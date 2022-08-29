#ifndef HID_H
#define HID_H
#include<stdint.h>
#include<pthread.h>
#include "vec.h"

typedef uint64_t uniq_t;

typedef struct {
    int event;
    int hidraw;
    uniq_t uniq;
} PhysicalDevice;

void * hid_thread();
void return_device(PhysicalDevice * dev);
PhysicalDevice get_device();

#endif
