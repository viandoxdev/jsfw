#include "net.h"

#include "util.h"

#include <stdio.h>
#include <string.h>


// Deserialize the message in buf, buf must be at least 4 aligned. Returns -1 on error, otherwise returns 0
// and writes result to dst
int msg_deserialize(const uint8_t *buf, size_t len, Message *restrict dst) {
    {
        if(len <= MAGIC_SIZE) {
            return -1;
        }

        if(*(MAGIC_TYPE*)buf != MAGIC_BEG) {
            printf("NET:     No magic in message\n");
            return -1;
        }

        buf += MAGIC_SIZE;
        len -= MAGIC_SIZE;
    }
    // Decrement len so that it becomes the len of the data without the code.
    if (len-- < 1)
        return -1;
    // This ensures that only a byte is read instead of a full enum value
    uint8_t        code_byte = buf[0];
    MessageCode    code      = (MessageCode)code_byte;
    uint32_t size = 0;

    uint16_t abs, rel, key, index, *buf16;

    switch (code) {
    case DeviceInfo:
        if (len < 7)
            return -1;
        // buf + 2: a byte for code and a byte for padding
        buf16 = (uint16_t *)(buf + 2);
        index = buf16[0];
        abs   = buf16[1];
        rel   = buf16[2];
        key   = buf16[3];
        buf += 12;
        if (MSS_DEVICE_INFO(abs, rel, key) > len)
            return -1;

        dst->device_info.code      = code;
        dst->device_info.index     = index;
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

        size = MSS_DEVICE_INFO(abs, rel, key) + 1;
        break;
    case DeviceReport:
        if (len < 7)
            return -1;

        // buf + 2: a byte for code and a byte of padding
        buf16 = (uint16_t *)(buf + 2);
        index = buf16[0];
        abs   = buf16[1];
        rel   = buf16[2];
        key   = buf16[3];
        buf += 12;
        if (len < MSS_DEVICE_REPORT(abs, rel, key))
            return -1;

        dst->device_report.code      = code;
        dst->device_report.index     = index;
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

        buf += align_4(key) - key;

        size = MSS_DEVICE_REPORT(abs, rel, key) + 1;
        break;
    case ControllerState:
        if (len < MSS_CONTROLLER_STATE)
            return -1;

        dst->code                          = code;
        dst->controller_state.index        = *(uint16_t *)(buf + 2);
        dst->controller_state.led[0]       = buf[4];
        dst->controller_state.led[1]       = buf[5];
        dst->controller_state.led[2]       = buf[6];
        dst->controller_state.small_rumble = buf[7];
        dst->controller_state.big_rumble   = buf[8];
        dst->controller_state.flash_on     = buf[9];
        dst->controller_state.flash_off    = buf[10];
        size = MSS_CONTROLLER_STATE + 1;
        buf += size;
        break;
    case Request: {
        if (len < 3)
            return -1;

        dst->code                  = code;
        dst->request.request_count = *(uint16_t *)(buf + 2);
        buf += 4; // 1 bytes for code, 1 byte for padding and 2 bytes for count

        int    count = dst->request.request_count;
        char **tags  = malloc(count * sizeof(char *));
        // The length of the message, will be updated as we read more.
        int expected_len = 3;

        for (int i = 0; i < dst->request.request_count; i++) {
            expected_len += 2;
            if (len < expected_len) {
                return -1;
            }

            uint16_t str_len = *(uint16_t *)buf;
            buf += 2;

            expected_len += align_2(str_len);
            if (len < expected_len) {
                return -1;
            }

            char *str    = malloc(str_len + 1);
            str[str_len] = '\0';

            strncpy(str, (char *)buf, str_len);

            tags[i] = str;

            buf += align_2(str_len);
        }

        dst->request.requests = tags;
        size = expected_len + 1;
        break;
    }
    case DeviceDestroy:
        if (len < MSS_DESTROY)
            return -1;

        dst->code          = code;
        dst->destroy.index = *(uint16_t *)(buf + 2);
        size = MSS_DESTROY + 1;
        buf += size;
        break;
    default:
        return -1;
    }

    if(size + MAGIC_SIZE > len + 1) {
        return -1;
    }

    if(*(MAGIC_TYPE*)buf != MAGIC_END) {
        printf("NET:     Magic not found\n");
        return -1;
    }

    return size + 2 * MAGIC_SIZE;
}

// Serialize the message msg in buf, buf must be at least 4 aligned. Returns -1 on error (buf not big enough);
int msg_serialize(uint8_t *restrict buf, size_t len, const Message *msg) {
    // If len is less than the two magic and the code we can't serialize any message
    if (len < MAGIC_SIZE * 2 + 1)
        return -1;

    *(MAGIC_TYPE*)buf = MAGIC_BEG;
    buf += MAGIC_SIZE;
    len -= MAGIC_SIZE + 1;

    uint16_t abs, rel, key, *buf16;
    uint32_t size;

    switch (msg->code) {
    case DeviceInfo:
        abs = msg->device_info.abs_count;
        rel = msg->device_info.rel_count;
        key = msg->device_info.key_count;
        if (len < MSS_DEVICE_INFO(abs, rel, key))
            return -1;

        // We begin 4 aligned
        buf[0] = (uint8_t)msg->code;
        // buf + 2: a byte for code and a byte for padding
        buf16 = (uint16_t *)(buf + 2);
        // 2 aligned here
        buf16[0] = msg->device_info.index;
        buf16[1] = abs;
        buf16[2] = rel;
        buf16[3] = key;
        buf += 12;

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

        size = MSS_DEVICE_INFO(abs, rel, key) + 1;
        break;
    case DeviceReport:
        abs = msg->device_report.abs_count;
        rel = msg->device_report.rel_count;
        key = msg->device_report.key_count;
        if (len < MSS_DEVICE_REPORT(abs, rel, key))
            return -1;

        buf[0] = (uint8_t)msg->code;
        // buf + 2: a byte for code and a byte for padding
        buf16    = (uint16_t *)(buf + 2);
        buf16[0] = msg->device_report.index;
        buf16[1] = abs;
        buf16[2] = rel;
        buf16[3] = key;
        buf += 12;
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

        size = MSS_DEVICE_REPORT(abs, rel, key) + 1;
        buf += align_4(key) - key;
        break;
    case ControllerState:
        if (len < MSS_CONTROLLER_STATE)
            return -1;

        buf[0] = (uint8_t)msg->code;

        *(uint16_t *)(buf + 2) = msg->controller_state.index;

        buf[4]  = msg->controller_state.led[0];
        buf[5]  = msg->controller_state.led[1];
        buf[6]  = msg->controller_state.led[2];
        buf[7]  = msg->controller_state.small_rumble;
        buf[8]  = msg->controller_state.big_rumble;
        buf[9]  = msg->controller_state.flash_on;
        buf[10] = msg->controller_state.flash_off;
        size = MSS_CONTROLLER_STATE + 1;
        buf += size;
        break;
    case Request: {
        int expected_len = MSS_REQUEST(msg->request.request_count);
        if (len < expected_len)
            return -1;

        buf[0]   = (uint8_t)msg->code;
        buf16    = (uint16_t *)(buf + 2);
        buf16[0] = msg->request.request_count;

        buf += 4;
        buf16++;

        for (int i = 0; i < msg->request.request_count; i++) {
            int str_len  = strlen(msg->request.requests[i]);
            int byte_len = align_2(str_len);
            *buf16++     = str_len;
            buf          = (uint8_t *)buf16;

            expected_len += byte_len + 2;
            if (len < expected_len) {
                return -1;
            }

            strncpy((char *)buf, msg->request.requests[i], str_len);
            buf += byte_len;
            // Buf has to be aligned here since byte_len is two aligned and we started off two aligned
            buf16 = (uint16_t *)buf;
        }

        size = expected_len;
        break;
    }
    case DeviceDestroy:
        if (len < MSS_DESTROY)
            return -1;

        buf[0] = (uint8_t)msg->code;

        *(uint16_t *)(buf + 2) = msg->controller_state.index;
        size = MSS_DESTROY + 1;
        buf += size;
        break;
    default:
        printf("ERR(msg_serialize): Trying to serialize unknown message of code %d\n", msg->code);
        return -1;
    }

    if(size + MAGIC_SIZE > len) {
        return -1;
    }

    *(MAGIC_TYPE*)buf = MAGIC_END;

    return size + MAGIC_SIZE * 2;
}

void msg_free(Message *msg) {
    if (msg->code == Request) {
        for (int i = 0; i < msg->request.request_count; i++) {
            free(msg->request.requests[i]);
        }
        free(msg->request.requests);
    }
}

void print_message_buffer(const uint8_t * buf, int len) {
    bool last_beg = false;
    for (int i = 0; i < len; i++) {
        if (i + MAGIC_SIZE <= len) {
            MAGIC_TYPE magic = *(MAGIC_TYPE *)(&buf[i]);
            if (magic == MAGIC_BEG) {
                printf(" \033[32m%08X\033[0m", magic);
                i += MAGIC_SIZE - 1;
                last_beg = true;
                continue;
            } else if (magic == MAGIC_END) {
                printf(" \033[32m%08X\033[0m", magic);
                i += MAGIC_SIZE - 1;
                continue;
            }
        }

        if (last_beg) {
            last_beg = false;
            printf(" \033[034m%02X\033[0m", buf[i]);
        } else {
            printf(" %02X", buf[i]);
        }
    }
}
