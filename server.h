// vi:ft=c
#ifndef SERVER_H_
#define SERVER_H_
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    // the 0 mac_address represents no filter
    uint64_t mac_address;
    // negative values means no filter
    int32_t vendor;
    // negative values means no filter
    int32_t product;
    bool    js;
} ControllerFilter;

typedef struct {
    ControllerFilter filter;
    char            *tag;
    bool             duplicate;
    bool             ps4_hidraw;
} ServerConfigController;

typedef struct {
    ServerConfigController *controllers;
    size_t                  controller_count;
    struct timespec         poll_interval;
    uint32_t                request_timeout;
} ServerConfig;

void server_run(uint16_t port, char *config_path);

#endif
