// vi: set shiftwidth=4 : set softtabstop=4
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "main.h"
#include "hid.h"

const char* USAGE[] = {
        "jsfw client [input] [address] [port]\n",
        "jsfw server [port]\n",
};
const size_t EVENT_SIZE = sizeof(struct js_event);

void joystick_debug(Joystick * js) {
    printf("Joystick");
    if(js->name) printf(" (%s)", js->name);
    printf(": %u buttons, %u axes\n", js->button_count, js->axis_count);
}

void panicf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    exit(1);
}

uint16_t parse_port(const char * str) {
    long long n = atoll(str);
    if(n < 0 || n > UINT16_MAX)
        panicf("Invalid port: Expected a number in the range 0..%d, got %lld\n", UINT16_MAX, n);
    return n;
}

void server(uint16_t port) {
    printf("Server (port: %u).\n", port);
    panicf("Uninplemented\n");
}

void client(char * input, char * address, uint16_t port) {
    hid_main();
    printf("JSFW Client (%s -> %s:%d)\n", input, address, port);
    int fd = open(input, O_RDONLY);
    if(fd < 0) panicf("Couldn't open %s", input);

    Joystick js = {};

    char name[256];
    int name_len = ioctl(fd, JSIOCGNAME(256), name);
    if(name_len >= 0) {
        js.name = malloc(name_len);
        if(js.name) strncpy(js.name, name, name_len);
    }

    ioctl(fd, JSIOCGBUTTONS, &js.button_count);
    ioctl(fd, JSIOCGAXES, &js.axis_count);

    joystick_debug(&js);
    
    struct js_event events[128];
    while(1) {
        int bytes = read(fd, events, EVENT_SIZE);
        if(bytes < EVENT_SIZE) {
            printf("Got %d bytes, expected at least %lu", bytes, EVENT_SIZE);
            continue;
        }
        int count = bytes / EVENT_SIZE;
        for(int i = 0; i < count; i++) {
            struct js_event event = events[i];
            printf("EV | type(%d) number(%d) value(%d) ts(%d)\n", event.type, event.number, event.value, event.time);
        }
    }
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

        if(argc < 5)
            panicf("Usage: %s", USAGE[0]);

        char * input = argv[2];
        char * address = argv[3];
        uint16_t port = parse_port(argv[4]);
        client(input, address, port);

    } else {
        printf("Unknown mode: '%s'\n", mode);
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    return 0;
}
