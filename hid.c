#include<stdlib.h>
#include<dirent.h>
#include<string.h>
#include<stdio.h>
#include<sys/ioctl.h>
#include<linux/input.h>
#include<fcntl.h>
#include<linux/uinput.h>
#include<linux/input.h>
#include<linux/joystick.h>
#include<stdbool.h>
#include<time.h>

#include "hid.h"
#include "vec.h"
#include "main.h"

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
    for(int i = 0; i < 17; i++) {
        char c = uniq[i];
        int digit;
        if(c >= '0' && c <= '9') digit = c - '0';
        else if(c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if(c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else continue;
        res <<= 4;
        res += digit;
    }
    return res;
}

bool filter_event(int fd, char * event) {
    char device_path[64];
    snprintf(device_path, 64, "/sys/class/input/%s/device", event);

    DIR * device_dir = opendir(device_path);
    struct dirent * device_dirent;

    bool found = false;
    while((device_dirent = readdir(device_dir)) != NULL) {
        if(device_dirent->d_type == DT_DIR && strncmp(device_dirent->d_name, "js", 2) == 0) {
            found = true;
            break;
        }
    }

    if(!found) {
        return false;
    }
    
    uint16_t info[4];
    ioctl(fd, EVIOCGID, info);
    return info[1] == 0x054c && info[2] == 0x05c4;
}

void poll_devices_init() {
    devices = vec_of(uniq_t);
    new_devices = vec_of(PhysicalDevice);
    devices_queue = vec_of(PhysicalDevice);
}

PhysicalDevice get_device() {
    pthread_mutex_lock(&devices_queue_mutex);
    if(devices_queue.len > 0){
        PhysicalDevice r;
        vec_pop(&devices_queue, &r);
        pthread_mutex_unlock(&devices_queue_mutex);

        return r;
    }
    while(devices_queue.len == 0) {
        pthread_cond_wait(&devices_queue_cond, &devices_queue_mutex);
    }
    
    PhysicalDevice res;
    vec_pop(&devices_queue, &res);
    if(devices_queue.len > 0) {
        pthread_cond_signal(&devices_queue_cond);
    }
    pthread_mutex_unlock(&devices_queue_mutex);
    return res;
}

void return_device(PhysicalDevice * dev) {
    close(dev->event);
    close(dev->hidraw);
    pthread_mutex_lock(&devices_mutex);
    for(int i = 0; i < devices.len; i++) {
        uniq_t * uniq = vec_get(&devices, i);
        if(*uniq == dev->uniq) {
            vec_remove(&devices, i, NULL);
            break;
        }
    }
    pthread_mutex_unlock(&devices_mutex);
}

void poll_devices() {
    vec_clear(&new_devices);

    DIR * input_dir = opendir("/sys/class/input");
    struct dirent * input;
    while((input = readdir(input_dir)) != NULL) {
        // Ignore if the entry isn't a linkg or doesn't start with event
        if(input->d_type != DT_LNK || strncmp(input->d_name, "event", 5) != 0) {
            continue;
        }

        PhysicalDevice dev = {};

        char event_path[64];
        snprintf(event_path, 64, "/dev/input/%s", input->d_name);

        dev.event = open(event_path, O_RDONLY);

        if(dev.event < 0) {
            continue;
        }

        char name[256] = {};
        ioctl(dev.event, EVIOCGNAME(256), name);

        if(!filter_event(dev.event, input->d_name)) goto skip;

        uniq_t uniq;
        {
            char uniq_str[17] = {};

            char uniq_path[256];
            snprintf(uniq_path, 256, "/sys/class/input/%s/device/uniq", input->d_name);

            int uniq_fd = open(uniq_path, O_RDONLY);

            if(uniq_fd < 0) goto skip;

            read(uniq_fd, uniq_str, 17);
            uniq = parse_uniq(uniq_str);

            close(uniq_fd);

            // If we couldn't parse the uniq (this assumes uniq can't be zero, which is probably alright)
            if(uniq == 0) goto skip;
        }

        bool found = false;

        pthread_mutex_lock(&devices_mutex);
        for(int i = 0; i < devices.len; i++) {
            uniq_t * dev_uniq = vec_get(&devices, i);
            if(*dev_uniq == uniq) {
                found = true;
                break;
            }
        }
        pthread_mutex_unlock(&devices_mutex);

        if(found) goto skip;

        dev.uniq = uniq;

        char hidraw_path[64];

        {
            char hidraw_path[256];
            snprintf(hidraw_path, 256, "/sys/class/input/%s/device/device/hidraw", input->d_name);

            DIR * hidraw_dir = opendir(hidraw_path);
            struct dirent * hidraw = NULL;
            while((hidraw = readdir(hidraw_dir)) != NULL) {
                if(strncmp(hidraw->d_name, "hidraw", 6) == 0) break;
            }

            if(hidraw == NULL) {
                printf("Couldn't get hidraw of %s", input->d_name);
                continue;
            }

            snprintf(hidraw_path, 64, "/dev/%s", hidraw->d_name);

            closedir(hidraw_dir);
        }

        dev.hidraw = open(hidraw_path, O_WRONLY);
        if(dev.hidraw < 0) goto skip;

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
    if(new_devices.len > 0) {
        pthread_mutex_lock(&devices_queue_mutex);
        vec_extend(&devices_queue, new_devices.data, new_devices.len);
        // Signal that there are new devices
        pthread_cond_signal(&devices_queue_cond);
        pthread_mutex_unlock(&devices_queue_mutex);
    }
}

void * hid_thread() {
    printf("HID: start\n");
    poll_devices_init();
    while(1) {
        poll_devices();
        struct timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
    }
    return NULL;
}
