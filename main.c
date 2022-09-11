#include "client.h"
#include "hid.h"
#include "server.h"
#include "util.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *USAGE[] = {
    "jsfw client [address] [port]\n",
    "jsfw server [port]\n",
};

// Start the server
void server(uint16_t port) {
    printf("[Server (0.0.0.0:%u)]\n\n", port);

    pthread_t thread;
    pthread_create(&thread, NULL, hid_thread, NULL);

    server_run(port);
}

// Start the client
void client(char *address, uint16_t port) {
    printf("[Client (%s:%d)]\n\n", address, port);
    client_run(address, port);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    char *mode = argv[1];

    if (strcmp(mode, "server") == 0) {
        if (argc < 3) {
            panicf("Usage: %s", USAGE[1]);
        }

        uint16_t port = parse_port(argv[2]);
        server(port);

    } else if (strcmp(mode, "client") == 0) {
        if (argc < 4) {
            panicf("Usage: %s", USAGE[0]);
        }

        char    *address = argv[2];
        uint16_t port    = parse_port(argv[3]);
        client(address, port);

    } else {
        printf("Unknown mode: '%s'\n", mode);
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    return 0;
}
