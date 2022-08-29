#include "net.h"

Message msg_device_info() {
    MessageDeviceInfo m;
    m.code = DeviceInfo;

    Message s;
    s.device_info = m;
    return s;
}

int msg_deserialize(const uint8_t * buf, size_t len, Message * dst) {
    // Decrement len so that it becomes the len of the data without the code.
    if(len-- < 1) return -1;
    // This ensures that only a byte is read instead of a full enum value
    uint8_t code_byte = buf[0];
    MessageCode code = (MessageCode) code_byte;

    switch(code) {
        case Heartbeat:
            if(MSS_HEARTBEAT > len) return -1;
            dst->code = code;
            dst->heartbeat.alive = buf[1];
            return 0;
        case DeviceInfo:
            if(MSS_DEVICE_INFO > len) return -1;
            dst->code = code;
            return 0;
        case DeviceReport:
            if(len < MSS_DEVICE_REPORT) return -1;
            dst->code = code;
            return 0;
        case DeviceDestroy:
            if(len < MSS_DEVICE_DESTROY) return -1;
            dst->code = code;
            return 0;
        case ControllerState:
            if(len < MSS_CONTROLLER_STATE) return -1;
            dst->code = code;
            dst->controller_state.led[0] =       buf[1];
            dst->controller_state.led[1] =       buf[2];
            dst->controller_state.led[2] =       buf[3];
            dst->controller_state.small_rumble = buf[4];
            dst->controller_state.big_rumble =   buf[5];
            dst->controller_state.flash_on =     buf[6];
            dst->controller_state.flash_off =    buf[7];
            return 0;
        default:
            return -1;
    }
}

// The indices have to match with msg_deserialize
int msg_serialize(uint8_t * buf, size_t len, Message msg) {
    switch(msg.code) {
        case Heartbeat:
            if(MSS_HEARTBEAT >= len) return -1;
            buf[0] = (uint8_t) msg.code;
            buf[1] = msg.heartbeat.alive;
            return 0;
        case DeviceInfo:
            if(MSS_DEVICE_INFO >= len) return -1;
            buf[0] = (uint8_t) msg.code;
            return 0;
        case DeviceReport:
            if(MSS_DEVICE_REPORT >= len) return -1;
            buf[0] = (uint8_t) msg.code;
            return 0;
        case DeviceDestroy:
            if(MSS_DEVICE_DESTROY >= len) return -1;
            buf[0] = (uint8_t) msg.code;
            return 0;
        case ControllerState:
            if(MSS_CONTROLLER_STATE >= len) return -1;
            buf[0] = (uint8_t) msg.code;
            buf[1] = msg.controller_state.led[0];
            buf[2] = msg.controller_state.led[1];
            buf[3] = msg.controller_state.led[2];
            buf[4] = msg.controller_state.small_rumble;
            buf[5] = msg.controller_state.big_rumble;
            buf[6] = msg.controller_state.flash_on;
            buf[7] = msg.controller_state.flash_off;
            return 0;
        default:
            return -1;
    }
}
