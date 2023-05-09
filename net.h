// Generated file, do not edit (its not like it'll explode if you do, but its better not to)
#ifndef NET_H
#define NET_H
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef unsigned char byte;
typedef uint64_t MsgMagic;

#define MSG_MAGIC_SIZE sizeof(MsgMagic)
static const MsgMagic MSG_MAGIC_START = 0xCAFEF00DBEEFDEAD;
static const MsgMagic MSG_MAGIC_END = 0xF00DBEEFCAFEDEAD;

typedef struct Abs {
    uint16_t id;
    uint32_t min;
    uint32_t max;
    uint32_t fuzz;
    uint32_t flat;
    uint32_t res;
} Abs;

typedef struct Key {
    uint16_t id;
} Key;

typedef struct Rel {
    uint16_t id;
} Rel;

typedef struct Tag {
    struct {
        uint16_t len;
        char *data;
    } name;
} Tag;

typedef struct TagList {
    struct {
        uint16_t len;
        struct Tag *data;
    } tags;
} TagList;

// Device

typedef enum DeviceTag {
    DeviceTagNone = 0,
    DeviceTagInfo = 1,
    DeviceTagReport = 2,
    DeviceTagControllerState = 3,
    DeviceTagRequest = 4,
    DeviceTagDestroy = 5,
} DeviceTag;

typedef struct DeviceInfo {
    DeviceTag tag;
    uint8_t slot;
    uint8_t index;
    struct {
        uint8_t len;
        struct Abs data[64];
    } abs;
    struct {
        uint8_t len;
        struct Rel data[16];
    } rel;
    struct {
        uint16_t len;
        struct Key data[768];
    } key;
} DeviceInfo;

typedef struct DeviceReport {
    DeviceTag tag;
    uint8_t slot;
    uint8_t index;
    struct {
        uint8_t len;
        uint32_t data[64];
    } abs;
    struct {
        uint8_t len;
        uint32_t data[16];
    } rel;
    struct {
        uint16_t len;
        uint8_t data[768];
    } key;
} DeviceReport;

typedef struct DeviceControllerState {
    DeviceTag tag;
    uint16_t index;
    uint8_t led[3];
    uint8_t small_rumble;
    uint8_t big_rumble;
    uint8_t flash_on;
    uint8_t flash_off;
} DeviceControllerState;

typedef struct DeviceRequest {
    DeviceTag tag;
    struct {
        uint16_t len;
        struct TagList *data;
    } requests;
    uint64_t _version;
} DeviceRequest;

typedef struct DeviceDestroy {
    DeviceTag tag;
    uint16_t index;
} DeviceDestroy;

typedef union DeviceMessage {
    DeviceTag tag;
    DeviceInfo info;
    DeviceReport report;
    DeviceControllerState controller_state;
    DeviceRequest request;
    DeviceDestroy destroy;
} DeviceMessage;

// Serialize the message msg to buffer dst of size len, returns the length of the serialized message, or -1 on error (buffer overflow)
int msg_device_serialize(byte *dst, size_t len, DeviceMessage *msg);
// Deserialize the message in the buffer src of size len into dst, return the length of the serialized message or -1 on error.
int msg_device_deserialize(const byte *src, size_t len, DeviceMessage *dst);

// Free the message (created by msg_device_deserialize)
void msg_device_free(DeviceMessage *msg);
#endif
