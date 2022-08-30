// vi: set ft=c
#ifndef HID_H
#define HID_H
#include "net.h"
#include "vec.h"
#include <linux/input.h>
#include <pthread.h>
#include <stdint.h>

typedef uint64_t uniq_t;

typedef struct {
    uint8_t abs_indices[ABS_CNT];
    uint8_t rel_indices[REL_CNT];
    uint8_t key_indices[KEY_CNT];
} DeviceMap;

typedef struct {
    int               event;
    int               hidraw;
    uniq_t            uniq;
    char             *name;
    DeviceMap         mapping;
    MessageDeviceInfo device_info;
} PhysicalDevice;

void          *hid_thread();
void           return_device(PhysicalDevice *dev);
PhysicalDevice get_device();
void           apply_controller_state(PhysicalDevice *dev, MessageControllerState *state);

#endif
