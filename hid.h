// vi:ft=c
#ifndef HID_H_
#define HID_H_
#include "net.h"

#include <linux/input-event-codes.h>
#include <stdint.h>

typedef uint64_t uniq_t;

typedef struct {
    uint16_t abs_indices[ABS_CNT];
    uint16_t rel_indices[REL_CNT];
    uint16_t key_indices[KEY_CNT];
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
