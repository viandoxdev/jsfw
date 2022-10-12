// vi:ft=c
#ifndef HID_H_
#define HID_H_
#include "net.h"
#include "server.h"

#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdint.h>

// Unique identifier for devices (provided by linux), May be the mac address
typedef uint64_t uniq_t;

// Mapping to go from index to id of events
// the id of an event is the code field of a input_event struct
// the index is given (somewhat arbitrarily) by hid.c::setup_device, this is done because ids are sparse
// and innefficient to transfer over network (especially for keys that can range from 0 to 700).
typedef struct {
    uint16_t abs_indices[ABS_CNT];
    uint16_t rel_indices[REL_CNT];
    uint16_t key_indices[KEY_CNT];
} DeviceMap;

// A struct representing a connected device
typedef struct {
    int               event;
    int               hidraw;
    uniq_t            uniq;
    uint64_t          id;
    char             *name;
    DeviceMap         mapping;
    MessageDeviceInfo device_info;
} PhysicalDevice;

typedef struct {
    PhysicalDevice         dev;
    ServerConfigController ctr;
} Controller;

void       *hid_thread(void *arg);
void        return_device(Controller *c);
void        forget_device(Controller *c);
Controller *get_device(char *tag, bool *stop);
void        apply_controller_state(Controller *c, MessageControllerState *state);

#endif
