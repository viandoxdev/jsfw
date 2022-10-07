#include "client.h"
#include "hid.h"
#include "server.h"
#include "util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *USAGE[] = {
    "jsfw client [address] [port] [config]\n",
    "jsfw server [port] [config]\n",
};

// Start the server
void server(uint16_t port, char *config_path) {
    printf("[Server (0.0.0.0:%u)] <- %s\n\n", port, config_path);

    server_run(port, config_path);
}

// Start the client
void client(char *address, uint16_t port, char *config_path) {
    printf("[Client (%s:%d)] <- %s\n\n", address, port, config_path);
    client_run(address, port, config_path);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    char *mode = argv[1];

    if (strcmp(mode, "server") == 0) {
        if (argc < 4) {
            panicf("Usage: %s", USAGE[1]);
        }

        uint16_t port        = parse_port(argv[2]);
        char    *config_path = argv[3];
        server(port, config_path);

    } else if (strcmp(mode, "client") == 0) {
        if (argc < 5) {
            panicf("Usage: %s", USAGE[0]);
        }

        char    *address     = argv[2];
        uint16_t port        = parse_port(argv[3]);
        char    *config_path = argv[4];
        client(address, port, config_path);

    } else {
        printf("Unknown mode: '%s'\n", mode);
        printf("Usage: %s", USAGE[0]);
        printf("       %s", USAGE[1]);
        return 1;
    }

    return 0;
}
