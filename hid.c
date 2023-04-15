#include "hid.h"

#include "const.h"
#include "server.h"
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

// List of ids of the currently known devices
static Vec known_devices;
// Queue of available devices, devices that can only be given to one client
static Vec available_devices;
// List of cloneable devices, devices that can be handed out to multiple clients
static Vec cloneable_devices;
// Mutex for devices
static pthread_mutex_t devices_mutex = PTHREAD_MUTEX_INITIALIZER;
// Condvar notified on devices update
static pthread_cond_t devices_cond = PTHREAD_COND_INITIALIZER;
// Mutex for devices
static pthread_mutex_t known_devices_mutex = PTHREAD_MUTEX_INITIALIZER;

static ServerConfig *config;

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

    uint8_t type_bits[EV_MAX]            = {0};
    uint8_t feat_bits[(KEY_MAX + 7) / 8] = {0};

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

bool filter_event(int fd, char *event, ControllerFilter *filter, uniq_t uniq) {
    if (filter->js) {
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

    if (filter->name != NULL) {
        char name[256] = {0};
        ioctl(fd, EVIOCGNAME(256), name);
        if (strcmp(name, filter->name) != 0) {
            return false;
        }
    }

    if (filter->uniq > 0 && uniq != filter->uniq) {
        return false;
    }

    struct input_id ids;
    ioctl(fd, EVIOCGID, &ids);

    if (filter->vendor > 0 && filter->vendor != ids.vendor)
        return false;
    if (filter->product > 0 && filter->product != ids.product)
        return false;

    return true;
}

// Initialize vectors for polling
void poll_devices_init(void) {
    known_devices     = vec_of(uint64_t);
    cloneable_devices = vec_of(Controller);
    available_devices = vec_of(Controller);
}

// Find index of tag that matches any of the tags specified in the tags array (of length tag_count), returns -1 otherwise
static int match_tags(char *tag, char **tags, size_t tag_count) {
    for (int i = 0; i < tag_count; i++) {
        if (strcmp(tag, tags[i]) == 0) {
            return i;
        }
    }

    return -1;
}

// Block to get a device, this is thread safe
// stop: additional condition to check before doing anything,
// if the condition is ever found to be true the function will return immediately with a NULL pointer.
bool get_device(char **tags, size_t tag_count, bool *stop, Controller *res, uint8_t *ref_index) {
    // Check if we can get one right away
    pthread_mutex_lock(&devices_mutex);

    while (1) {
        if (*stop) {
            pthread_mutex_unlock(&devices_mutex);
            return false;
        }

        for (int i = 0; i < available_devices.len; i++) {
            Controller *c     = vec_get(&available_devices, i);
            int         index = match_tags(c->ctr.tag, tags, tag_count);
            if (index >= 0) {
                *ref_index = index;
                *res       = *c;
                vec_remove(&available_devices, i, NULL);
                pthread_mutex_unlock(&devices_mutex);
                return true;
            }
        }

        for (int i = 0; i < cloneable_devices.len; i++) {
            Controller *c     = vec_get(&cloneable_devices, i);
            int         index = match_tags(c->ctr.tag, tags, tag_count);
            if (index >= 0) {
                *ref_index = index;
                *res       = *c;
                pthread_mutex_unlock(&devices_mutex);
                return true;
            }
        }

        // Wait on condvar until there's a device and we can unlock the mutex
        pthread_cond_wait(&devices_cond, &devices_mutex);
    }
}

// Return a device that isn't used anymore, this really only makes sense for non cloneable devices.
void return_device(Controller *c) {
    // If device is cloneable there is nothing to return
    if (c->ctr.duplicate) {
        return;
    }

    pthread_mutex_lock(&devices_mutex);
    vec_push(&available_devices, c);
    // Signal that there are new devices
    pthread_cond_broadcast(&devices_cond);
    pthread_mutex_unlock(&devices_mutex);
}

// Forget about a broken device. This invalidates the reference to the controller
void forget_device(Controller *c) {
    pthread_mutex_lock(&known_devices_mutex);

    // If controller is cloneable we need to remove it from the cloneable list
    if (c->ctr.duplicate) {
        for (int i = 0; i < cloneable_devices.len; i++) {
            Controller *d = *(Controller **)vec_get(&cloneable_devices, i);
            if (d->dev.id == c->dev.id) {
                vec_remove(&cloneable_devices, i, NULL);
                break;
            }
        }
    }

    // Free the name if it was allocated
    if (c->dev.name != NULL && c->dev.name != DEVICE_DEFAULT_NAME) {
        printf("HID:     Forgetting device '%s' (%lu)\n", c->dev.name, c->dev.id);
        free(c->dev.name);
    } else {
        printf("HID:     Forgetting device %lu\n", c->dev.id);
    }

    // try to close the file descriptor, they may be already closed if the device was unpugged.
    close(c->dev.event);
    close(c->dev.hidraw);

    // Safely remove device from the known device list
    for (int i = 0; i < known_devices.len; i++) {
        uint64_t *id = vec_get(&known_devices, i);
        if (*id == c->dev.id) {
            vec_remove(&known_devices, i, NULL);
            break;
        }
    }
    pthread_mutex_unlock(&known_devices_mutex);
}

uint64_t parse_event_name(const char *event) { return atol(event + 5); }

// Find all available devices and pick up on new ones
void poll_devices(void) {
    // loop over all entries of /sys/class/input
    DIR           *input_dir = opendir(FSROOT "/sys/class/input");
    struct dirent *input;

    while ((input = readdir(input_dir)) != NULL) {
        // Ignore if the entry isn't a link or doesn't start with event
        if (input->d_type != DT_LNK || strncmp(input->d_name, "event", 5) != 0) {
            continue;
        }

        PhysicalDevice dev;
        dev.hidraw = -1;
        dev.uniq   = 0;
        dev.id     = parse_event_name(input->d_name);

        // Open /dev/input/eventXX
        {
            char event_path[64];
            snprintf(event_path, 64, FSROOT "/dev/input/%s", input->d_name);

            dev.event = open(event_path, O_RDONLY);

            if (dev.event < 0) { // Ignore device if we couldn't open
                continue;
            }
        }

        // Try to get the name, default to DEFAULT_NAME if impossible
        char *name;
        {
            static char name_buf[256] = {0};
            if (ioctl(dev.event, EVIOCGNAME(256), name_buf) >= 0) {
                name = name_buf;
            } else {
                name = (char *)DEVICE_DEFAULT_NAME;
            }
        }

        // Try to get uniq, drop device if we can't
        {
            char uniq_str[17] = {0};

            ioctl(dev.event, EVIOCGUNIQ(17), uniq_str);
            dev.uniq = parse_uniq(uniq_str);
        }

        // Used for linear searches
        bool found;

        // Filter devices according server config
        ServerConfigController *ctr;
        {
            found = false;
            for (int i = 0; i < config->controller_count; i++) {
                ctr = &config->controllers[i];

                if (filter_event(dev.event, input->d_name, &ctr->filter, dev.uniq)) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                goto skip;
            }
        }

        // Check if we already know of this device
        {
            found = false;

            pthread_mutex_lock(&known_devices_mutex);
            for (int i = 0; i < known_devices.len; i++) {
                uint64_t *id = vec_get(&known_devices, i);
                if (*id == dev.id) {
                    found = true;
                    break;
                }
            }
            pthread_mutex_unlock(&known_devices_mutex);

            if (found) { // Device isn't new
                goto skip;
            }
        }

        // Look for hidraw if the device should have one (Dualshock 4 only, with ps4_hidraw property set)
        if (ctr->ps4_hidraw) {
            // Attempt to find the path
            char hidraw_path[64];
            {
                char hidraw_dir_path[256];
                snprintf(hidraw_dir_path, 256, FSROOT "/sys/class/input/%s/device/device/hidraw", input->d_name);

                DIR           *hidraw_dir = opendir(hidraw_dir_path);
                struct dirent *hidraw     = NULL;
                while ((hidraw = readdir(hidraw_dir)) != NULL) {
                    if (strncmp(hidraw->d_name, "hidraw", 6) == 0) {
                        break;
                    }
                }

                if (hidraw == NULL) {
                    printf("HID:    Couldn't get hidraw of %s", input->d_name);
                    goto skip;
                }

                snprintf(hidraw_path, 64, FSROOT "/dev/%s", hidraw->d_name);

                closedir(hidraw_dir);
            }
            // Try to open
            dev.hidraw = open(hidraw_path, O_WRONLY);
            if (dev.hidraw < 0) {
                goto skip;
            }
        }

        // Allocate for name (only now to avoid unecessary allocations)
        if (name != DEVICE_DEFAULT_NAME) {
            dev.name = malloc(256);

            if (dev.name == NULL) {
                dev.name = (char *)DEVICE_DEFAULT_NAME;
            } else {
                strcpy(dev.name, name);
            }
        }

        // This code is only run if the device has passed all filters and requirements
        {
            setup_device(&dev);
            Controller c = {.dev = dev, .ctr = *ctr};

            pthread_mutex_lock(&known_devices_mutex);
            vec_push(&known_devices, &c.dev.id);
            pthread_mutex_unlock(&known_devices_mutex);

            printf("HID:     New device, %s [%s] (%s: %lu)\n", name, ctr->tag, input->d_name, dev.id);

            if (ctr->duplicate) {
                pthread_mutex_lock(&devices_mutex);
                vec_push(&cloneable_devices, &c);
                // Signal that there are new cloneable devices
                pthread_cond_broadcast(&devices_cond);
                pthread_mutex_unlock(&devices_mutex);
            } else {
                pthread_mutex_lock(&devices_mutex);
                vec_push(&available_devices, &c);
                // Signal that there are new devices
                pthread_cond_broadcast(&devices_cond);
                pthread_mutex_unlock(&devices_mutex);
            }
        }
        // Continue here avoids running cleanup code
        continue;

    // close open file descriptor and continue
    skip:
        close(dev.event);
    };
    closedir(input_dir);
}

// "Execute" a MessageControllerState: set the led color, rumble and flash using the hidraw interface (Dualshock 4 only)
void apply_controller_state(Controller *c, MessageControllerState *state) {
    if (c->ctr.ps4_hidraw && c->dev.hidraw < 0) {
        printf("HID:     Trying to apply controller state on incompatible device (%lu)\n", c->dev.id);
        return;
    }

    printf("HID:     (%lu) Controller state: #%02x%02x%02x flash: (%d, %d) rumble: (%d, %d)\n", c->dev.id, state->led[0],
           state->led[1], state->led[2], state->flash_on, state->flash_off, state->small_rumble, state->big_rumble);

    uint8_t buf[32] = {0x05, 0xff, 0x00, 0x00};

    buf[4]  = state->small_rumble;
    buf[5]  = state->big_rumble;
    buf[6]  = state->led[0];
    buf[7]  = state->led[1];
    buf[8]  = state->led[2];
    buf[9]  = state->flash_on;
    buf[10] = state->flash_off;

    write(c->dev.hidraw, buf, 32);
    if (state->flash_on == 0 && state->flash_off == 0) {
        // May not be necessary
        fsync(c->dev.hidraw);
        // Send a second time, to reenable the led
        write(c->dev.hidraw, buf, 32);
    };
}

// Body of the hid thread
void *hid_thread(void *arg) {
    printf("HID:     start\n");
    config = arg;

    poll_devices_init();
    while (1) {
        poll_devices();
        nanosleep(&config->poll_interval, NULL);
    }

    return NULL;
}
