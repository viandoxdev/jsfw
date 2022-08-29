#ifndef NET_H
#define NET_H
#include<stdint.h>
#include<stdlib.h>

typedef enum {
    Heartbeat = 0,
    DeviceInfo = 1,
    DeviceReport = 2,
    DeviceDestroy = 3,
    ControllerState = 4,
} MessageCode;

typedef struct {
    MessageCode code;
    uint8_t alive;
} MessageHeartbeat;
#define MSS_HEARTBEAT 1
// MSS -> Message Serialized Size:
// Size of the data of the message when serialized (no alignment / padding)

typedef struct {
    MessageCode code;
} MessageDeviceInfo;
#define MSS_DEVICE_INFO 0

typedef struct {
    MessageCode code;
} MessageDeviceReport;
#define MSS_DEVICE_REPORT 0

typedef struct {
    MessageCode code;
} MessageDeviceDestroy;
#define MSS_DEVICE_DESTROY 0

typedef struct  {
    MessageCode code;
    uint8_t led[3];
    uint8_t small_rumble;
    uint8_t big_rumble;
    uint8_t flash_on;
    uint8_t flash_off;
} MessageControllerState;
#define MSS_CONTROLLER_STATE 7

typedef union {
    MessageCode code;
    MessageHeartbeat heartbeat;
    MessageDeviceInfo device_info;
    MessageDeviceReport device_report;
    MessageDeviceDestroy device_destroy;
    MessageControllerState controller_state;
} Message;

int msg_deserialize(const uint8_t * buf, size_t len, Message * dst);
int msg_serialize(uint8_t * buf, size_t len, Message msg);

#endif
