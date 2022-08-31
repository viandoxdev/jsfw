#include "net.h"

#include <stdio.h>

Message msg_device_info() {
    MessageDeviceInfo m;
    m.code = DeviceInfo;

    Message s;
    s.device_info = m;
    return s;
}

// Deserialize the message in buf, buf must be at least 4 aligned. Returns -1 on error, otherwise returns 0
// and writes result to dst
int msg_deserialize(const uint8_t *buf, size_t len, Message * restrict dst) {
    // Decrement len so that it becomes the len of the data without the code.
    if (len-- < 1)
        return -1;
    // This ensures that only a byte is read instead of a full enum value
    uint8_t     code_byte = buf[0];
    MessageCode code      = (MessageCode)code_byte;

    uint16_t abs, rel, key, *buf16;

    switch (code) {
    case DeviceInfo:
        if (len < 7)
            return -1;
        // buf + 2: a byte for code and a byte for padding
        buf16 = (uint16_t *)(buf + 2);
        abs   = buf16[0];
        rel   = buf16[1];
        key   = buf16[2];
        buf += 8;
        if (MSS_DEVICE_INFO(abs, rel, key) > len)
            return -1;

        dst->device_info.code      = code;
        dst->device_info.abs_count = abs;
        dst->device_info.rel_count = rel;
        dst->device_info.key_count = key;

        // SOA in c but serialized as AOS
        for (int i = 0; i < abs; i++) {
            // buf + 4: 2 bytes for id and 2 bytes of padding
            uint32_t *buf32 = (uint32_t *)(buf + 4);

            dst->device_info.abs_id[i]   = *(uint16_t *)buf;
            dst->device_info.abs_min[i]  = buf32[0];
            dst->device_info.abs_max[i]  = buf32[1];
            dst->device_info.abs_fuzz[i] = buf32[2];
            dst->device_info.abs_flat[i] = buf32[3];
            dst->device_info.abs_res[i]  = buf32[4];

            buf += 24;
        }

        for (int i = 0; i < rel; i++) {
            dst->device_info.rel_id[i] = *(uint16_t *)buf;
            buf += 2;
        }

        for (int i = 0; i < key; i++) {
            dst->device_info.key_id[i] = *(uint16_t *)buf;
            buf += 2;
        }

        return 0;
    case DeviceReport:
        if (len < 7)
            return -1;

        // buf + 2: a byte for code and a byte of padding
        buf16 = (uint16_t *)(buf + 2);
        abs   = buf16[0];
        rel   = buf16[1];
        key   = buf16[2];
        buf += 8;
        if (len < MSS_DEVICE_REPORT(abs, rel, key))
            return -1;

        dst->device_report.code      = code;
        dst->device_report.abs_count = abs;
        dst->device_report.rel_count = rel;
        dst->device_report.key_count = key;

        for (int i = 0; i < abs; i++) {
            dst->device_report.abs[i] = *(uint32_t *)buf;
            buf += 4;
        }

        for (int i = 0; i < rel; i++) {
            dst->device_report.rel[i] = *(uint32_t *)buf;
            buf += 4;
        }

        for (int i = 0; i < key; i++)
            dst->device_report.key[i] = *(buf++);

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

// Serialize the message msg in buf, buf must be at least 4 aligned. Returns -1 on error (buf not big enough);
int msg_serialize(uint8_t * restrict buf, size_t len, const Message *msg) {
    // If len is 0 we can't serialize any message
    if (len-- == 0)
        return -1;

    uint16_t abs, rel, key, *buf16;

    switch (msg->code) {
    case DeviceInfo:
        abs = msg->device_info.abs_count;
        rel = msg->device_info.rel_count;
        key = msg->device_info.key_count;
        if (len < MSS_DEVICE_INFO(abs, rel, key))
            return -1;

        // We begin 4 aligned
        buf[0]   = (uint8_t)msg->code;
        // buf + 2: a byte for code and a byte for padding
        buf16    = (uint16_t *)(buf + 2);
        // 2 aligned here
        buf16[0] = abs;
        buf16[1] = rel;
        buf16[2] = key;
        buf += 8;

        // Back to 4 aligned
        for (int i = 0; i < abs; i++) {
            // buf + 4: 2 bytes for id and 2 bytes of padding
            uint32_t *buf32 = (uint32_t *)(buf + 4);

            *(uint16_t *)buf = msg->device_info.abs_id[i];

            buf32[0] = msg->device_info.abs_min[i];
            buf32[1] = msg->device_info.abs_max[i];
            buf32[2] = msg->device_info.abs_fuzz[i];
            buf32[3] = msg->device_info.abs_flat[i];
            buf32[4] = msg->device_info.abs_res[i];

            buf += 24;
        }
        // Still 4 aligned
        for (int i = 0; i < rel; i++) {
            *(uint16_t *)buf = msg->device_info.rel_id[i];
            buf += 2;
        }

        for (int i = 0; i < key; i++) {
            *(uint16_t *)buf = msg->device_info.key_id[i];
            buf += 2;
        }

        return MSS_DEVICE_INFO(abs, rel, key) + 1;
    case DeviceReport:
        abs = msg->device_report.abs_count;
        rel = msg->device_report.rel_count;
        key = msg->device_report.key_count;
        if (len < MSS_DEVICE_REPORT(abs, rel, key))
            return -1;

        buf[0]   = (uint8_t)msg->code;
        // buf + 2: a byte for code and a byte for padding
        buf16    = (uint16_t *)(buf + 2);
        buf16[0] = abs;
        buf16[1] = rel;
        buf16[2] = key;
        buf += 8;
        // We're 4 aligned already
        for (int i = 0; i < abs; i++) {
            *(uint32_t *)buf = msg->device_report.abs[i];
            buf += 4;
        }
        // Still 4 aligned
        for (int i = 0; i < rel; i++) {
            *(uint32_t *)buf = msg->device_report.rel[i];
            buf += 4;
        }
        // Doesn't matter since we're writing individual bytes
        for (int i = 0; i < key; i++)
            *(buf++) = msg->device_report.key[i];

        return MSS_DEVICE_REPORT(abs, rel, key) + 1;
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
        printf("ERR(msg_serialize): Trying to serialize unknown message of code %d\n", msg->code);
        return -1;
    }
}
