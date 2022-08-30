#include "net.h"

Message msg_device_info() {
    MessageDeviceInfo m;
    m.code = DeviceInfo;

    Message s;
    s.device_info = m;
    return s;
}

int msg_deserialize(const uint8_t *buf, size_t len, Message *dst) {
    // Decrement len so that it becomes the len of the data without the code.
    if (len-- < 1)
        return -1;
    // This ensures that only a byte is read instead of a full enum value
    uint8_t     code_byte = buf[0];
    MessageCode code      = (MessageCode)code_byte;

    switch (code) {
    case DeviceInfo:
        if (len < 3)
            return -1;
        uint8_t abs = buf[1];
        uint8_t rel = buf[2];
        uint8_t key = buf[3];
        buf += 4;
        if (MSS_DEVICE_INFO(abs, rel, key) > len)
            return -1;

        dst->code                  = code;
        dst->device_info.abs_count = abs;
        dst->device_info.rel_count = rel;
        dst->device_info.key_count = key;

        // SOA in c but serialized as AOS
        for (int i = 0; i < abs; i++) {
            uint32_t *buf32 = (uint32_t *)(buf + 1);

            dst->device_info.abs_id[i]   = buf[0];
            dst->device_info.abs_min[i]  = buf32[0];
            dst->device_info.abs_max[i]  = buf32[1];
            dst->device_info.abs_fuzz[i] = buf32[2];
            dst->device_info.abs_flat[i] = buf32[3];
            dst->device_info.abs_res[i]  = buf32[4];

            buf += 21;
        }

        for (int i = 0; i < rel; i++)
            dst->device_info.rel_id[i] = *(buf++);

        for (int i = 0; i < key; i++)
            dst->device_info.key_id[i] = *(buf++);

        return 0;
    case DeviceReport:
        if (len < MSS_DEVICE_REPORT)
            return -1;
        dst->code = code;
        return 0;
    case DeviceDestroy:
        if (len < MSS_DEVICE_DESTROY)
            return -1;
        dst->code = code;
        return 0;
    case ControllerState:
        if (len < MSS_CONTROLLER_STATE)
            return -1;

        dst->code                          = code;
        dst->controller_state.led[0]       = buf[1];
        dst->controller_state.led[1]       = buf[2];
        dst->controller_state.led[2]       = buf[3];
        dst->controller_state.small_rumble = buf[4];
        dst->controller_state.big_rumble   = buf[5];
        dst->controller_state.flash_on     = buf[6];
        dst->controller_state.flash_off    = buf[7];
        return 0;
    default:
        return -1;
    }
}

// The indices have to match with msg_deserialize
int msg_serialize(uint8_t *buf, size_t len, Message *msg) {
    // If len is 0 we can't serialize any message
    if (len-- == 0)
        return -1;

    switch (msg->code) {
    case DeviceInfo:; // semicolon needed here
        uint8_t abs = msg->device_info.abs_count;
        uint8_t rel = msg->device_info.rel_count;
        uint8_t key = msg->device_info.key_count;
        if (len < MSS_DEVICE_INFO(abs, rel, len))
            return -1;

        buf[0] = (uint8_t)msg->code;
        buf[1] = abs;
        buf[2] = rel;
        buf[3] = key;
        buf += 4;

        for (int i = 0; i < abs; i++) {
            uint32_t *buf32 = (uint32_t *)(buf + 1);

            buf[0]   = msg->device_info.abs_id[i];
            buf32[0] = msg->device_info.abs_min[i];
            buf32[1] = msg->device_info.abs_max[i];
            buf32[2] = msg->device_info.abs_fuzz[i];
            buf32[3] = msg->device_info.abs_flat[i];
            buf32[4] = msg->device_info.abs_res[i];

            buf += 21;
        }

        for (int i = 0; i < rel; i++)
            *(buf++) = msg->device_info.rel_id[i];

        for (int i = 0; i < key; i++)
            *(buf++) = msg->device_info.key_id[i];

        return MSS_DEVICE_INFO(abs, rel, key) + 1;
    case DeviceReport:
        if (len < MSS_DEVICE_REPORT)
            return -1;

        buf[0] = (uint8_t)msg->code;
        return MSS_DEVICE_REPORT + 1;
    case DeviceDestroy:
        if (len < MSS_DEVICE_DESTROY)
            return -1;

        buf[0] = (uint8_t)msg->code;
        return MSS_DEVICE_DESTROY + 1;
    case ControllerState:
        if (len < MSS_CONTROLLER_STATE)
            return -1;

        buf[0] = (uint8_t)msg->code;
        buf[1] = msg->controller_state.led[0];
        buf[2] = msg->controller_state.led[1];
        buf[3] = msg->controller_state.led[2];
        buf[4] = msg->controller_state.small_rumble;
        buf[5] = msg->controller_state.big_rumble;
        buf[6] = msg->controller_state.flash_on;
        buf[7] = msg->controller_state.flash_off;
        return MSS_CONTROLLER_STATE + 1;
    default:
        return -1;
    }
}
