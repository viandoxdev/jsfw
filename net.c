// Generated file, do not edit (its not like it'll explode if you do, but its better not to)
#include "net.h"
#include <stdio.h>

__attribute__((unused)) static int abs_serialize(struct Abs val, byte *buf);
__attribute__((unused)) static int abs_deserialize(struct Abs *val, const byte *buf);
__attribute__((unused)) static void abs_free(struct Abs val);
__attribute__((unused)) static int key_serialize(struct Key val, byte *buf);
__attribute__((unused)) static int key_deserialize(struct Key *val, const byte *buf);
__attribute__((unused)) static void key_free(struct Key val);
__attribute__((unused)) static int rel_serialize(struct Rel val, byte *buf);
__attribute__((unused)) static int rel_deserialize(struct Rel *val, const byte *buf);
__attribute__((unused)) static void rel_free(struct Rel val);
__attribute__((unused)) static int tag_list_serialize(struct TagList val, byte *buf);
__attribute__((unused)) static int tag_list_deserialize(struct TagList *val, const byte *buf);
__attribute__((unused)) static void tag_list_free(struct TagList val);
__attribute__((unused)) static int tag_serialize(struct Tag val, byte *buf);
__attribute__((unused)) static int tag_deserialize(struct Tag *val, const byte *buf);
__attribute__((unused)) static void tag_free(struct Tag val);

static int abs_serialize(struct Abs val, byte *buf) {
    byte * base_buf = buf;
    *(uint32_t *)&buf[0] = val.min;
    *(uint32_t *)&buf[4] = val.max;
    *(uint32_t *)&buf[8] = val.fuzz;
    *(uint32_t *)&buf[12] = val.flat;
    *(uint32_t *)&buf[16] = val.res;
    *(uint16_t *)&buf[20] = val.id;
    buf += 24;
    return (int)(buf - base_buf);
}
static int abs_deserialize(struct Abs *val, const byte *buf) {
    const byte * base_buf = buf;
    val->min = *(uint32_t *)&buf[0];
    val->max = *(uint32_t *)&buf[4];
    val->fuzz = *(uint32_t *)&buf[8];
    val->flat = *(uint32_t *)&buf[12];
    val->res = *(uint32_t *)&buf[16];
    val->id = *(uint16_t *)&buf[20];
    buf += 24;
    return (int)(buf - base_buf);
}
static void abs_free(struct Abs val) { }

static int key_serialize(struct Key val, byte *buf) {
    byte * base_buf = buf;
    *(uint16_t *)&buf[0] = val.id;
    buf += 2;
    return (int)(buf - base_buf);
}
static int key_deserialize(struct Key *val, const byte *buf) {
    const byte * base_buf = buf;
    val->id = *(uint16_t *)&buf[0];
    buf += 2;
    return (int)(buf - base_buf);
}
static void key_free(struct Key val) { }

static int rel_serialize(struct Rel val, byte *buf) {
    byte * base_buf = buf;
    *(uint16_t *)&buf[0] = val.id;
    buf += 2;
    return (int)(buf - base_buf);
}
static int rel_deserialize(struct Rel *val, const byte *buf) {
    const byte * base_buf = buf;
    val->id = *(uint16_t *)&buf[0];
    buf += 2;
    return (int)(buf - base_buf);
}
static void rel_free(struct Rel val) { }

static int tag_list_serialize(struct TagList val, byte *buf) {
    byte * base_buf = buf;
    *(uint16_t *)&buf[0] = val.tags.len;
    buf += 2;
    for(size_t i = 0; i < val.tags.len; i++) {
        typeof(val.tags.data[i]) e0 = val.tags.data[i];
        buf += tag_serialize(e0, &buf[0]);
    }
    buf = (byte*)(((((uintptr_t)buf - 1) >> 1) + 1) << 1);
    return (int)(buf - base_buf);
}
static int tag_list_deserialize(struct TagList *val, const byte *buf) {
    const byte * base_buf = buf;
    val->tags.len = *(uint16_t *)&buf[0];
    buf += 2;
    val->tags.data = malloc(val->tags.len * sizeof(typeof(*val->tags.data)));
    for(size_t i = 0; i < val->tags.len; i++) {
        typeof(&val->tags.data[i]) e0 = &val->tags.data[i];
        buf += tag_deserialize(e0, &buf[0]);
    }
    buf = (byte*)(((((uintptr_t)buf - 1) >> 1) + 1) << 1);
    return (int)(buf - base_buf);
}
static void tag_list_free(struct TagList val) {
    for(size_t i = 0; i < val.tags.len; i++) {
        typeof(val.tags.data[i]) e0 = val.tags.data[i];
        tag_free(e0);
    }
    free(val.tags.data);
}

static int tag_serialize(struct Tag val, byte *buf) {
    byte * base_buf = buf;
    *(uint16_t *)&buf[0] = val.name.len;
    buf += 2;
    for(size_t i = 0; i < val.name.len; i++) {
        typeof(val.name.data[i]) e0 = val.name.data[i];
        *(char *)&buf[0] = e0;
        buf += 1;
    }
    buf = (byte*)(((((uintptr_t)buf - 1) >> 1) + 1) << 1);
    return (int)(buf - base_buf);
}
static int tag_deserialize(struct Tag *val, const byte *buf) {
    const byte * base_buf = buf;
    val->name.len = *(uint16_t *)&buf[0];
    buf += 2;
    val->name.data = malloc(val->name.len * sizeof(typeof(*val->name.data)));
    for(size_t i = 0; i < val->name.len; i++) {
        typeof(&val->name.data[i]) e0 = &val->name.data[i];
        *e0 = *(char *)&buf[0];
        buf += 1;
    }
    buf = (byte*)(((((uintptr_t)buf - 1) >> 1) + 1) << 1);
    return (int)(buf - base_buf);
}
static void tag_free(struct Tag val) {
    free(val.name.data);
}

int msg_device_serialize(byte *buf, size_t len, DeviceMessage *msg) {
    const byte *base_buf = buf;
    if(len < 2 * MSG_MAGIC_SIZE)
        return -1;
    *(MsgMagic*)buf = MSG_MAGIC_START;
    buf += MSG_MAGIC_SIZE;
    switch(msg->tag) {
    case DeviceTagNone:
        break;
    case DeviceTagInfo: {
        *(uint16_t *)buf = DeviceTagInfo;
        *(uint16_t *)&buf[2] = msg->info.key.len;
        *(uint8_t *)&buf[4] = msg->info.slot;
        *(uint8_t *)&buf[5] = msg->info.index;
        *(uint8_t *)&buf[6] = msg->info.abs.len;
        *(uint8_t *)&buf[7] = msg->info.rel.len;
        buf += 8;
        for(size_t i = 0; i < msg->info.abs.len; i++) {
            typeof(msg->info.abs.data[i]) e0 = msg->info.abs.data[i];
            buf += abs_serialize(e0, &buf[0]);
        }
        for(size_t i = 0; i < msg->info.rel.len; i++) {
            typeof(msg->info.rel.data[i]) e0 = msg->info.rel.data[i];
            buf += rel_serialize(e0, &buf[0]);
        }
        for(size_t i = 0; i < msg->info.key.len; i++) {
            typeof(msg->info.key.data[i]) e0 = msg->info.key.data[i];
            buf += key_serialize(e0, &buf[0]);
        }
        buf = (byte*)(((((uintptr_t)buf - 1) >> 3) + 1) << 3);
        break;
    }
    case DeviceTagReport: {
        *(uint16_t *)buf = DeviceTagReport;
        *(uint16_t *)&buf[2] = msg->report.key.len;
        *(uint8_t *)&buf[4] = msg->report.slot;
        *(uint8_t *)&buf[5] = msg->report.index;
        *(uint8_t *)&buf[6] = msg->report.abs.len;
        *(uint8_t *)&buf[7] = msg->report.rel.len;
        buf += 8;
        for(size_t i = 0; i < msg->report.abs.len; i++) {
            typeof(msg->report.abs.data[i]) e0 = msg->report.abs.data[i];
            *(uint32_t *)&buf[0] = e0;
            buf += 4;
        }
        for(size_t i = 0; i < msg->report.rel.len; i++) {
            typeof(msg->report.rel.data[i]) e0 = msg->report.rel.data[i];
            *(uint32_t *)&buf[0] = e0;
            buf += 4;
        }
        for(size_t i = 0; i < msg->report.key.len; i++) {
            typeof(msg->report.key.data[i]) e0 = msg->report.key.data[i];
            *(uint8_t *)&buf[0] = e0;
            buf += 1;
        }
        buf = (byte*)(((((uintptr_t)buf - 1) >> 3) + 1) << 3);
        break;
    }
    case DeviceTagControllerState: {
        *(uint16_t *)buf = DeviceTagControllerState;
        *(uint16_t *)&buf[2] = msg->controller_state.index;
        *(uint8_t *)&buf[4] = msg->controller_state.led[0];
        *(uint8_t *)&buf[5] = msg->controller_state.led[1];
        *(uint8_t *)&buf[6] = msg->controller_state.led[2];
        *(uint8_t *)&buf[7] = msg->controller_state.small_rumble;
        *(uint8_t *)&buf[8] = msg->controller_state.big_rumble;
        *(uint8_t *)&buf[9] = msg->controller_state.flash_on;
        *(uint8_t *)&buf[10] = msg->controller_state.flash_off;
        buf += 16;
        break;
    }
    case DeviceTagRequest: {
        *(uint16_t *)buf = DeviceTagRequest;
        msg->request._version = 1UL;
        *(uint64_t *)&buf[8] = msg->request._version;
        *(uint16_t *)&buf[16] = msg->request.requests.len;
        buf += 18;
        for(size_t i = 0; i < msg->request.requests.len; i++) {
            typeof(msg->request.requests.data[i]) e0 = msg->request.requests.data[i];
            buf += tag_list_serialize(e0, &buf[0]);
        }
        buf = (byte*)(((((uintptr_t)buf - 1) >> 3) + 1) << 3);
        break;
    }
    case DeviceTagDestroy: {
        *(uint16_t *)buf = DeviceTagDestroy;
        *(uint16_t *)&buf[2] = msg->destroy.index;
        buf += 8;
        break;
    }
    }
    *(MsgMagic*)buf = MSG_MAGIC_END;
    buf += MSG_MAGIC_SIZE;
    if(buf > base_buf + len)
        return -1;
    return (int)(buf - base_buf);
}

int msg_device_deserialize(const byte *buf, size_t len, DeviceMessage *msg) {
    const byte *base_buf = buf;
    if(len < 2 * MSG_MAGIC_SIZE)
        return -1;
    if(*(MsgMagic*)buf != MSG_MAGIC_START)
        return -1;
    buf += MSG_MAGIC_SIZE;
    DeviceTag tag = *(uint16_t*)buf;
    switch(tag) {
    case DeviceTagNone:
        break;
    case DeviceTagInfo: {
        msg->tag = DeviceTagInfo;
        msg->info.key.len = *(uint16_t *)&buf[2];
        msg->info.slot = *(uint8_t *)&buf[4];
        msg->info.index = *(uint8_t *)&buf[5];
        msg->info.abs.len = *(uint8_t *)&buf[6];
        msg->info.rel.len = *(uint8_t *)&buf[7];
        buf += 8;
        for(size_t i = 0; i < msg->info.abs.len; i++) {
            typeof(&msg->info.abs.data[i]) e0 = &msg->info.abs.data[i];
            buf += abs_deserialize(e0, &buf[0]);
        }
        for(size_t i = 0; i < msg->info.rel.len; i++) {
            typeof(&msg->info.rel.data[i]) e0 = &msg->info.rel.data[i];
            buf += rel_deserialize(e0, &buf[0]);
        }
        for(size_t i = 0; i < msg->info.key.len; i++) {
            typeof(&msg->info.key.data[i]) e0 = &msg->info.key.data[i];
            buf += key_deserialize(e0, &buf[0]);
        }
        buf = (byte*)(((((uintptr_t)buf - 1) >> 3) + 1) << 3);
        break;
    }
    case DeviceTagReport: {
        msg->tag = DeviceTagReport;
        msg->report.key.len = *(uint16_t *)&buf[2];
        msg->report.slot = *(uint8_t *)&buf[4];
        msg->report.index = *(uint8_t *)&buf[5];
        msg->report.abs.len = *(uint8_t *)&buf[6];
        msg->report.rel.len = *(uint8_t *)&buf[7];
        buf += 8;
        for(size_t i = 0; i < msg->report.abs.len; i++) {
            typeof(&msg->report.abs.data[i]) e0 = &msg->report.abs.data[i];
            *e0 = *(uint32_t *)&buf[0];
            buf += 4;
        }
        for(size_t i = 0; i < msg->report.rel.len; i++) {
            typeof(&msg->report.rel.data[i]) e0 = &msg->report.rel.data[i];
            *e0 = *(uint32_t *)&buf[0];
            buf += 4;
        }
        for(size_t i = 0; i < msg->report.key.len; i++) {
            typeof(&msg->report.key.data[i]) e0 = &msg->report.key.data[i];
            *e0 = *(uint8_t *)&buf[0];
            buf += 1;
        }
        buf = (byte*)(((((uintptr_t)buf - 1) >> 3) + 1) << 3);
        break;
    }
    case DeviceTagControllerState: {
        msg->tag = DeviceTagControllerState;
        msg->controller_state.index = *(uint16_t *)&buf[2];
        msg->controller_state.led[0] = *(uint8_t *)&buf[4];
        msg->controller_state.led[1] = *(uint8_t *)&buf[5];
        msg->controller_state.led[2] = *(uint8_t *)&buf[6];
        msg->controller_state.small_rumble = *(uint8_t *)&buf[7];
        msg->controller_state.big_rumble = *(uint8_t *)&buf[8];
        msg->controller_state.flash_on = *(uint8_t *)&buf[9];
        msg->controller_state.flash_off = *(uint8_t *)&buf[10];
        buf += 16;
        break;
    }
    case DeviceTagRequest: {
        msg->tag = DeviceTagRequest;
        msg->request._version = *(uint64_t *)&buf[8];
        msg->request.requests.len = *(uint16_t *)&buf[16];
        buf += 18;
        msg->request.requests.data = malloc(msg->request.requests.len * sizeof(typeof(*msg->request.requests.data)));
        for(size_t i = 0; i < msg->request.requests.len; i++) {
            typeof(&msg->request.requests.data[i]) e0 = &msg->request.requests.data[i];
            buf += tag_list_deserialize(e0, &buf[0]);
        }
        buf = (byte*)(((((uintptr_t)buf - 1) >> 3) + 1) << 3);
        if(msg->request._version != 1UL) {
            printf("Mismatched version: peers aren't the same version, expected 1 got %lu.\n", msg->request._version);
            msg_device_free(msg);
            return -1;
        }
        break;
    }
    case DeviceTagDestroy: {
        msg->tag = DeviceTagDestroy;
        msg->destroy.index = *(uint16_t *)&buf[2];
        buf += 8;
        break;
    }
    }
    if(*(MsgMagic*)buf != MSG_MAGIC_END) {
        msg_device_free(msg);
        return -1;
    }
    buf += MSG_MAGIC_SIZE;
    if(buf > base_buf + len) {
        msg_device_free(msg);
        return -1;
    }
    return (int)(buf - base_buf);
}

void msg_device_free(DeviceMessage *msg) {
    switch(msg->tag) {
    case DeviceTagNone:
        break;
    case DeviceTagInfo: {
        break;
    }
    case DeviceTagReport: {
        break;
    }
    case DeviceTagControllerState: {
        break;
    }
    case DeviceTagRequest: {
        for(size_t i = 0; i < msg->request.requests.len; i++) {
            typeof(msg->request.requests.data[i]) e0 = msg->request.requests.data[i];
            tag_list_free(e0);
        }
        free(msg->request.requests.data);
        break;
    }
    case DeviceTagDestroy: {
        break;
    }
    }
}
