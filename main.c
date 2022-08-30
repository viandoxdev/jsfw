#include<stdio.h>
#include<sys/ioctl.h>
#include<linux/joystick.h>
#include<stdint.h>
#include<fcntl.h>
#include<stdlib.h>
#include<string.h>
#include<stdarg.h>
#include<unistd.h>
#include<pthread.h>

#include "main.h"
#include "hid.h"
#include "server.h"
#include "util.h"

const char* USAGE[] = {
        "jsfw client [address] [port]\n",
        "jsfw server [port]\n",
};
const size_t EVENT_SIZE = sizeof(struct js_event);

uint16_t parse_port(const char * str) {
    long long n = atoll(str);
    if(n <= 0 || n > UINT16_MAX)
        panicf("Invalid port: Expected a number in the range 1..%d, got '%s'\n", UINT16_MAX, str);
    return n;
}

void server(uint16_t port) {
    printf("Server (port: %u).\n", port);

    pthread_t thread;
    pthread_create(&thread, NULL, hid_thread, NULL);
    server_run(port);
}

void client(char * address, uint16_t port) {
    printf("JSFW Client (%s:%d)\n", address, port);
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    char* mode = argv[1];

    if(strcmp(mode, "server") == 0) {

        if(argc < 3)
            panicf("Usage: %s", USAGE[1]);

        uint16_t port = parse_port(argv[2]);
        server(port);

    } else if(strcmp(mode, "client") == 0) {

        if(argc < 4)
            panicf("Usage: %s", USAGE[0]);

        char * address = argv[2];
        uint16_t port = parse_port(argv[3]);
        client(address, port);

    } else {
        printf("Unknown mode: '%s'\n", mode);
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    return 0;
}
