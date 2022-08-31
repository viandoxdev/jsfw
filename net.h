// vi:ft=c
#ifndef NET_H_
#define NET_H_
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum {
    DeviceInfo      = 1,
    DeviceReport    = 2,
    DeviceDestroy   = 3,
    ControllerState = 4,
} MessageCode;

// Alignment 4
typedef struct {
    MessageCode code;
    // + 1 byte of padding

    uint16_t abs_count;
    uint16_t rel_count;
    uint16_t key_count;

    uint16_t abs_id[ABS_CNT];
    // + 2 bytes of padding per abs
    uint32_t abs_min[ABS_CNT];
    uint32_t abs_max[ABS_CNT];
    uint32_t abs_fuzz[ABS_CNT];
    uint32_t abs_flat[ABS_CNT];
    uint32_t abs_res[ABS_CNT];

    uint16_t rel_id[REL_CNT];

    uint16_t key_id[KEY_CNT];
} MessageDeviceInfo;
#define MSS_DEVICE_INFO(abs, rel, key) (8 + abs * 24 + rel * 2 + key * 2 + 1)
// MSS -> Message Serialized Size:
// Size of the data of the message when serialized (no alignment / padding)

// 4 aligned
typedef struct {
    MessageCode code;
    // + 1 byte of padding

    uint16_t abs_count;
    uint16_t rel_count;
    uint16_t key_count;

    uint32_t abs[ABS_CNT];
    uint32_t rel[REL_CNT];
    uint8_t  key[KEY_CNT];
} MessageDeviceReport;
#define MSS_DEVICE_REPORT(abs, rel, key) (6 + abs * 4 + rel * 4 + key * 1 + 1)

// 1 aligned
typedef struct {
    MessageCode code;

    uint8_t led[3];
    uint8_t small_rumble;
    uint8_t big_rumble;
    uint8_t flash_on;
    uint8_t flash_off;
} MessageControllerState;
#define MSS_CONTROLLER_STATE 7

typedef union {
    MessageCode            code;
    MessageDeviceInfo      device_info;
    MessageDeviceReport    device_report;
    MessageControllerState controller_state;
} Message;

int msg_deserialize(const uint8_t *buf, size_t len, Message *restrict dst);
int msg_serialize(uint8_t *restrict buf, size_t len, const Message *msg);

#endif
