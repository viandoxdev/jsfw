#include "hid.h"

#include "const.h"
#include "util.h"
#include "vec.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

// List of uniq of the currently known devices
static Vec devices;
// List of the new devices of a poll, static to keep the allocation alive
static Vec new_devices;
// Queue of devices to be taken by connections
static Vec devices_queue;
// Mutex for the device queue
static pthread_mutex_t devices_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
// Condvar notified on device queue update
static pthread_cond_t devices_queue_cond = PTHREAD_COND_INITIALIZER;
// Mutex for devices
static pthread_mutex_t devices_mutex = PTHREAD_MUTEX_INITIALIZER;

// uniqs are just hexadecimal numbers with colons in between each byte
uniq_t parse_uniq(char uniq[17]) {
    uniq_t res = 0;
    for (int i = 0; i < 17; i++) {
        char c = uniq[i];
        int  digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            continue;
        res <<= 4;
        res += digit;
    }
    return res;
}

// Finish setup of a partially initialized device (set device_info and mapping)
void setup_device(PhysicalDevice *dev) {
    dev->device_info.code      = DeviceInfo;
    dev->device_info.abs_count = 0;
    dev->device_info.rel_count = 0;
    dev->device_info.key_count = 0;

    for (int i = 0; i < ABS_CNT; i++)
        dev->mapping.abs_indices[i] = -1;
    for (int i = 0; i < REL_CNT; i++)
        dev->mapping.rel_indices[i] = -1;
    for (int i = 0; i < KEY_CNT; i++)
        dev->mapping.key_indices[i] = -1;

    uint8_t type_bits[EV_MAX]            = {};
    uint8_t feat_bits[(KEY_MAX + 7) / 8] = {};

    ioctl(dev->event, EVIOCGBIT(0, EV_MAX), type_bits);
    // Loop over all event types
    for (int type = 0; type < EV_MAX; type++) {
        // Ignore if the the device doesn't have any of this event type
        if (!bit_set(type_bits, type)) {
            continue;
        }
        // Clear feat_bits to only have the features of the current type
        memset(feat_bits, 0, sizeof(feat_bits));
        ioctl(dev->event, EVIOCGBIT(type, KEY_MAX), feat_bits);

        // Loop over "instances" of type (i.e Each axis of a controller for EV_ABS)
        for (int i = 0; i < KEY_MAX; i++) {
            // "instances" don't have to be consecutive (this is why we do all this instead of just worrying
            // about the count)
            if (!bit_set(feat_bits, i)) {
                continue;
            }

            if (type == EV_ABS) {
                struct input_absinfo abs;
                ioctl(dev->event, EVIOCGABS(i), &abs);

                uint16_t index = dev->device_info.abs_count++;

                dev->device_info.abs_min[index]  = abs.minimum;
                dev->device_info.abs_max[index]  = abs.maximum;
                dev->device_info.abs_fuzz[index] = abs.fuzz;
                dev->device_info.abs_flat[index] = abs.flat;
                dev->device_info.abs_res[index]  = abs.resolution;
                // Bidirectional mapping id <-> index
                // We need this to avoid wasting space in packets because ids are sparse
                dev->device_info.abs_id[index] = i;
                dev->mapping.abs_indices[i]    = index;
            } else if (type == EV_REL) {
                uint16_t index = dev->device_info.rel_count++;

                dev->device_info.rel_id[index] = i;
                dev->mapping.rel_indices[i]    = index;
            } else if (type == EV_KEY) {
                uint16_t index = dev->device_info.key_count++;

                dev->device_info.key_id[index] = i;
                dev->mapping.key_indices[i]    = index;
            }
        }
    }
}

// Function used to filter out devices that we don't want.
// This is pretty arbritrary
bool filter_event(int fd, char *event) {
    // Check for existance of a js* directory in /sys/class/input/eventXX/device
    // This is used to filter out the touchpad of PS4 controller (which have the same product and vendor id as
    // the controller)
    {
        char device_path[64];
        snprintf(device_path, 64, "/sys/class/input/%s/device", event);

        DIR           *device_dir = opendir(device_path);
        struct dirent *device_dirent;

        bool found = false;
        while ((device_dirent = readdir(device_dir)) != NULL) {
            if (device_dirent->d_type == DT_DIR && strncmp(device_dirent->d_name, "js", 2) == 0) {
                found = true;
                break;
            }
        }

        closedir(device_dir);

        if (!found) {
            return false;
        }
    }

    // Check product and vendor id 054c:05c4 => Dualshock 4
    uint16_t info[4];
    ioctl(fd, EVIOCGID, info);
    return info[1] == 0x054c && info[2] == 0x05c4;
}

// Initialize vectors for polling
void poll_devices_init() {
    devices       = vec_of(uniq_t);
    new_devices   = vec_of(PhysicalDevice);
    devices_queue = vec_of(PhysicalDevice);
}

// Block to get a device, this is thread safe
PhysicalDevice get_device() {
    // Check if we can get one right away
    pthread_mutex_lock(&devices_queue_mutex);
    if (devices_queue.len > 0) {
        PhysicalDevice r;
        vec_pop(&devices_queue, &r);
        pthread_mutex_unlock(&devices_queue_mutex);

        return r;
    }
    // Wait on condvar until there's a device and we can unlock the mutex
    while (devices_queue.len == 0) {
        pthread_cond_wait(&devices_queue_cond, &devices_queue_mutex);
    }

    // Take a device from the queue
    PhysicalDevice res;
    vec_pop(&devices_queue, &res);

    // Signal another thread if there are still device(s) left in the queue
    if (devices_queue.len > 0) {
        pthread_cond_signal(&devices_queue_cond);
    }

    pthread_mutex_unlock(&devices_queue_mutex);
    return res;
}

// Forget about a device. This is used on two cases:
//  - If the connection to a client is lost, the device is forgotten, picked up by the next device poll, and
//  put back in the queue
//  - If the device dies (i.e unplugged), the connection to the client is closed and the device forgotten.
//
// This is thread safe
void return_device(PhysicalDevice *dev) {
    if (dev->name != NULL && dev->name != DEVICE_DEFAULT_NAME) {
        // Free the name if it was allocated
        printf("HID:     Returning device '%s' (%012lx)\n", dev->name, dev->uniq);
        free(dev->name);
    } else {
        printf("HID:     Returning device %012lx\n", dev->uniq);
    }
    // try to close the file descriptor, they may be already closed if the device was unpugged.
    close(dev->event);
    close(dev->hidraw);

    // Safely remove device from the known device list
    pthread_mutex_lock(&devices_mutex);
    for (int i = 0; i < devices.len; i++) {
        uniq_t *uniq = vec_get(&devices, i);
        if (*uniq == dev->uniq) {
            vec_remove(&devices, i, NULL);
            break;
        }
    }
    pthread_mutex_unlock(&devices_mutex);
}

// Find all available devices and pick up on new ones
void poll_devices() {
    vec_clear(&new_devices);

    // loop over all entries of /sys/class/input
    DIR           *input_dir = opendir("/sys/class/input");
    struct dirent *input;
    while ((input = readdir(input_dir)) != NULL) {
        // Ignore if the entry isn't a link or doesn't start with event
        if (input->d_type != DT_LNK || strncmp(input->d_name, "event", 5) != 0) {
            continue;
        }

        PhysicalDevice dev;

        // Open /dev/input/eventXX
        char event_path[64];
        snprintf(event_path, 64, "/dev/input/%s", input->d_name);

        dev.event = open(event_path, O_RDONLY);

        if (dev.event < 0) { // Ignore device if we couldn't open
            continue;
        }

        // Try to get the name, default to DEFAULT_NAME if impossible
        char        name_buf[256] = {};
        const char *name;
        if (ioctl(dev.event, EVIOCGNAME(256), name_buf) >= 0) {
            name = name_buf;
        } else {
            name = DEVICE_DEFAULT_NAME;
        }

        // Filter events we don't care about
        if (!filter_event(dev.event, input->d_name)) {
            goto skip;
        }

        // Try to get uniq, drop device if we can't
        uniq_t uniq;
        {
            char uniq_str[17] = {};

            ioctl(dev.event, EVIOCGUNIQ(17), uniq_str);
            uniq = parse_uniq(uniq_str);

            // If we couldn't parse the uniq (this assumes uniq can't be zero, which is probably alright)
            if (uniq == 0) {
                goto skip;
            }
        }

        // Check if we already know of this device
        bool found = false;

        pthread_mutex_lock(&devices_mutex);
        for (int i = 0; i < devices.len; i++) {
            uniq_t *dev_uniq = vec_get(&devices, i);
            if (*dev_uniq == uniq) {
                found = true;
                break;
            }
        }
        pthread_mutex_unlock(&devices_mutex);

        if (found) { // Device isn't new
            goto skip;
        }

        dev.uniq = uniq;

        // Try to find hidraw path for the device, drop the device if we can't
        char hidraw_path[64];
        {
            char hidraw_dir_path[256];
            snprintf(hidraw_dir_path, 256, "/sys/class/input/%s/device/device/hidraw", input->d_name);

            DIR           *hidraw_dir = opendir(hidraw_dir_path);
            struct dirent *hidraw     = NULL;
            while ((hidraw = readdir(hidraw_dir)) != NULL) {
                if (strncmp(hidraw->d_name, "hidraw", 6) == 0) {
                    break;
                }
            }

            if (hidraw == NULL) {
                printf("Couldn't get hidraw of %s", input->d_name);
                continue;
            }

            snprintf(hidraw_path, 64, "/dev/%s", hidraw->d_name);

            closedir(hidraw_dir);
        }

        dev.hidraw = open(hidraw_path, O_WRONLY);
        if (dev.hidraw < 0) {
            goto skip;
        }

        if (name != DEVICE_DEFAULT_NAME) {
            dev.name = malloc(256);

            if (dev.name == NULL) {
                dev.name = (char *)DEVICE_DEFAULT_NAME;
            } else {
                strcpy(dev.name, name);
            }
        }

        setup_device(&dev);

        pthread_mutex_lock(&devices_mutex);
        vec_push(&devices, &uniq);
        pthread_mutex_unlock(&devices_mutex);

        vec_push(&new_devices, &dev);

        printf("HID:     New device, %s (%s: %012lx)\n", name, input->d_name, dev.uniq);
        // Continue here to avoid running cleanup code of skip
        continue;

    // close open file descriptor and continue
    skip:
        close(dev.event);
        continue;
    };
    closedir(input_dir);

    // Safely add new devices to the queue
    if (new_devices.len > 0) {
        pthread_mutex_lock(&devices_queue_mutex);
        vec_extend(&devices_queue, new_devices.data, new_devices.len);
        // Signal that there are new devices
        pthread_cond_signal(&devices_queue_cond);
        pthread_mutex_unlock(&devices_queue_mutex);
    }
}

// "Execute" a MessageControllerState: set the led color, rumble and flash using the hidraw interface
void apply_controller_state(PhysicalDevice *dev, MessageControllerState *state) {
    printf("HID:     (%012lx) Controller state: #%02x%02x%02x flash: (%d, %d) rumble: (%d, %d)\n", dev->uniq,
           state->led[0], state->led[1], state->led[2], state->flash_on, state->flash_off,
           state->small_rumble, state->big_rumble);

    uint8_t buf[32] = {0x05, 0xff, 0x00, 0x00};

    buf[4]  = state->small_rumble;
    buf[5]  = state->big_rumble;
    buf[6]  = state->led[0];
    buf[7]  = state->led[1];
    buf[8]  = state->led[2];
    buf[9]  = state->flash_on;
    buf[10] = state->flash_off;

    write(dev->hidraw, buf, 32);
    if (state->flash_on == 0 && state->flash_off == 0) {
        // May not be necessary
        fsync(dev->hidraw);
        // Send a second time, to reenable the led
        write(dev->hidraw, buf, 32);
    };
}

// Body of the hid thread
void *hid_thread() {
    printf("HID:     start\n");

    poll_devices_init();
    while (1) {
        poll_devices();

        nanosleep(&POLL_DEVICE_INTERVAL, NULL);
    }

    return NULL;
}
