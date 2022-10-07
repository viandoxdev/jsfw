// vi:ft=c
#ifndef CLIENT_H_
#define CLIENT_H_
#include <stdint.h>
#include <time.h>

void client_run(char *address, uint16_t port, char *config_path);

typedef struct {
    char   *tag;
    int32_t device_vendor;
    int32_t device_product;
    char   *device_name;
} ClientController;

typedef struct {
    ClientController *controllers;
    size_t                  controller_count;

    char           *fifo_path;
    struct timespec retry_delay;
} ClientConfig;

#endif
