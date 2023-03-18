// vi:ft=c
#ifndef NET_H_
#define NET_H_
#include "util.h"

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdlib.h>

#define MAGIC_TYPE uint32_t
#define MAGIC_SIZE sizeof(MAGIC_TYPE)
static const MAGIC_TYPE MAGIC_BEG = 0xDEADCAFE;
static const MAGIC_TYPE MAGIC_END = 0xCAFEDEAD;

typedef enum {
    NoMessage       = 0,
    DeviceInfo      = 1,
    DeviceReport    = 2,
    DeviceDestroy   = 3,
    ControllerState = 4,
    Request         = 5,
} MessageCode;

// Alignment 4
typedef struct {
    MessageCode code;
    // + 1 byte of padding

    uint16_t index;

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
#define MSS_DEVICE_INFO(abs, rel, key) (10 + abs * 24 + rel * 2 + key * 2 + 1)
// MSS -> Message Serialized Size:
// Size of the data of the message when serialized (no alignment / padding)

// 4 aligned
typedef struct {
    MessageCode code;
    // + 1 byte of padding
    uint16_t index;

    uint16_t abs_count;
    uint16_t rel_count;
    uint16_t key_count;

    uint32_t abs[ABS_CNT];
    uint32_t rel[REL_CNT];
    uint8_t  key[KEY_CNT];
} MessageDeviceReport;
#define MSS_DEVICE_REPORT(abs, rel, key) (11 + abs * 4 + rel * 4 + align_4(key))

// 1 aligned
typedef struct {
    MessageCode code;
    // + 1 byte of padding

    uint16_t index;
    uint8_t  led[3];
    uint8_t  small_rumble;
    uint8_t  big_rumble;
    uint8_t  flash_on;
    uint8_t  flash_off;
} MessageControllerState;
#define MSS_CONTROLLER_STATE 10

typedef struct {
    char   **tags;
    uint16_t count;
} TagList;

typedef struct {
    MessageCode code;
    // + 1 byte of padding

    TagList *requests;
    uint16_t request_count;
} MessageRequest;
#define MSS_REQUEST(count) (2 + 2 * count + 1)

typedef struct {
    MessageCode code;
    // + 1 byte of padding

    uint16_t index;
} MessageDestroy;
#define MSS_DESTROY 3

typedef union {
    MessageCode            code;
    MessageRequest         request;
    MessageDestroy         destroy;
    MessageDeviceInfo      device_info;
    MessageDeviceReport    device_report;
    MessageControllerState controller_state;
} Message;

int  msg_deserialize(const uint8_t *buf, size_t len, Message *restrict dst);
int  msg_serialize(uint8_t *restrict buf, size_t len, const Message *msg);
void msg_free(Message *msg);
void print_message_buffer(const uint8_t *buf, int len);

#endif
