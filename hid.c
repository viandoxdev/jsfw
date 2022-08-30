#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

#include "hid.h"
#include "main.h"
#include "vec.h"

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

static char *DEFAULT_NAME = "Unnamed Device";

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

static inline bool bit_set(uint8_t *bits, int i) { return bits[i / 8] & (1 << (i % 8)); }

void setup_device(PhysicalDevice *dev) {
    dev->device_info.code      = DeviceInfo;
    dev->device_info.abs_count = 0;
    dev->device_info.rel_count = 0;
    dev->device_info.key_count = 0;

    uint8_t bits[EV_MAX]       = {};
    uint8_t feat_bits[KEY_MAX] = {};
    ioctl(dev->event, EVIOCGBIT(0, EV_MAX), bits);
    for (int i = 0; i < EV_MAX; i++) {
        if (bit_set(bits, i)) {
            ioctl(dev->event, EVIOCGBIT(i, KEY_MAX), feat_bits);
            for (int j = 0; j < KEY_MAX; j++) {
                if (bit_set(feat_bits, j)) {
                    if (i == EV_ABS) {
                        struct input_absinfo abs;
                        ioctl(dev->event, EVIOCGABS(j), &abs);
                        uint8_t index                    = dev->device_info.abs_count++;
                        dev->device_info.abs_id[index]   = j;
                        dev->device_info.abs_min[index]  = abs.minimum;
                        dev->device_info.abs_max[index]  = abs.maximum;
                        dev->device_info.abs_fuzz[index] = abs.fuzz;
                        dev->device_info.abs_flat[index] = abs.flat;
                        dev->device_info.abs_res[index]  = abs.resolution;
                        dev->mapping.abs_indices[j]      = index;
                    } else if (i == EV_REL) {
                        uint8_t index                  = dev->device_info.rel_count++;
                        dev->device_info.rel_id[index] = j;
                        dev->mapping.rel_indices[j]    = index;
                    } else if (i == EV_KEY) {
                        uint8_t index                  = dev->device_info.key_count++;
                        dev->device_info.key_id[index] = j;
                        dev->mapping.key_indices[j]    = index;
                    }
                }
            }
        }
    }
}

bool filter_event(int fd, char *event) {
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

    uint16_t info[4];
    ioctl(fd, EVIOCGID, info);
    return info[1] == 0x054c && info[2] == 0x05c4;
}

void poll_devices_init() {
    devices       = vec_of(uniq_t);
    new_devices   = vec_of(PhysicalDevice);
    devices_queue = vec_of(PhysicalDevice);
}

PhysicalDevice get_device() {
    pthread_mutex_lock(&devices_queue_mutex);
    if (devices_queue.len > 0) {
        PhysicalDevice r;
        vec_pop(&devices_queue, &r);
        pthread_mutex_unlock(&devices_queue_mutex);

        return r;
    }
    while (devices_queue.len == 0) {
        pthread_cond_wait(&devices_queue_cond, &devices_queue_mutex);
    }

    PhysicalDevice res;
    vec_pop(&devices_queue, &res);
    if (devices_queue.len > 0) {
        pthread_cond_signal(&devices_queue_cond);
    }
    pthread_mutex_unlock(&devices_queue_mutex);
    return res;
}

void return_device(PhysicalDevice *dev) {
    if (dev->name != NULL && dev->name != DEFAULT_NAME) {
        printf("HID: Returning device '%s' (%012lx)\n", dev->name, dev->uniq);
        free(dev->name);
    } else {
        printf("HID: Returning device %012lx\n", dev->uniq);
    }
    close(dev->event);
    close(dev->hidraw);
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

void poll_devices() {
    vec_clear(&new_devices);

    DIR           *input_dir = opendir("/sys/class/input");
    struct dirent *input;
    while ((input = readdir(input_dir)) != NULL) {
        // Ignore if the entry isn't a linkg or doesn't start with event
        if (input->d_type != DT_LNK || strncmp(input->d_name, "event", 5) != 0) {
            continue;
        }

        PhysicalDevice dev;

        char event_path[64];
        snprintf(event_path, 64, "/dev/input/%s", input->d_name);

        dev.event = open(event_path, O_RDONLY);

        if (dev.event < 0) {
            continue;
        }

        char  name_buf[256] = {};
        char *name;
        if (ioctl(dev.event, EVIOCGNAME(256), name_buf) >= 0)
            name = name_buf;
        else
            name = DEFAULT_NAME;

        if (!filter_event(dev.event, input->d_name))
            goto skip;

        uniq_t uniq;
        {
            char uniq_str[17] = {};

            ioctl(dev.event, EVIOCGUNIQ(17), uniq_str);
            uniq = parse_uniq(uniq_str);

            // If we couldn't parse the uniq (this assumes uniq can't be zero, which is probably alright)
            if (uniq == 0)
                goto skip;
        }

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

        if (found)
            goto skip;

        dev.uniq = uniq;

        char hidraw_path[64];

        {
            char hidraw_path[256];
            snprintf(hidraw_path, 256, "/sys/class/input/%s/device/device/hidraw", input->d_name);

            DIR           *hidraw_dir = opendir(hidraw_path);
            struct dirent *hidraw     = NULL;
            while ((hidraw = readdir(hidraw_dir)) != NULL) {
                if (strncmp(hidraw->d_name, "hidraw", 6) == 0)
                    break;
            }

            if (hidraw == NULL) {
                printf("Couldn't get hidraw of %s", input->d_name);
                continue;
            }

            snprintf(hidraw_path, 64, "/dev/%s", hidraw->d_name);

            closedir(hidraw_dir);
        }

        dev.hidraw = open(hidraw_path, O_WRONLY);
        if (dev.hidraw < 0)
            goto skip;

        dev.name = malloc(256);
        if (dev.name == NULL)
            dev.name = DEFAULT_NAME;
        else
            strcpy(dev.name, name);

        setup_device(&dev);

        pthread_mutex_lock(&devices_mutex);
        vec_push(&devices, &uniq);
        pthread_mutex_unlock(&devices_mutex);
        vec_push(&new_devices, &dev);

        printf("HID: New device, %s (%s: %012lx)\n", name, input->d_name, dev.uniq);
        continue;

        // close open file descriptor and continue
    skip:
        close(dev.event);
        continue;
    };

    closedir(input_dir);
    if (new_devices.len > 0) {
        pthread_mutex_lock(&devices_queue_mutex);
        vec_extend(&devices_queue, new_devices.data, new_devices.len);
        // Signal that there are new devices
        pthread_cond_signal(&devices_queue_cond);
        pthread_mutex_unlock(&devices_queue_mutex);
    }
}

void apply_controller_state(PhysicalDevice *dev, MessageControllerState *state) {
    uint8_t buf[32] = {0x05, 0xff, 0x00, 0x00};

    buf[4]  = state->small_rumble;
    buf[5]  = state->big_rumble;
    buf[6]  = state->led[0];
    buf[7]  = state->led[1];
    buf[8]  = state->led[2];
    buf[9]  = state->flash_on;
    buf[10] = state->flash_off;

    write(dev->hidraw, buf, 32);
    if(state->flash_on == 0 && state->flash_off == 0) {
        fsync(dev->hidraw);
        // Send a second time because it doesn't work otherwise
        write(dev->hidraw, buf, 32);
    };
}

void *hid_thread() {
    printf("HID: start\n");
    poll_devices_init();
    while (1) {
        poll_devices();
        struct timespec ts;
        ts.tv_sec  = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
    }
    return NULL;
}
