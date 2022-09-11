#include "const.h"
#include "hid.h"
#include "net.h"
#include "util.h"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Arguments for a connection thread
struct Connection {
    int      socket;
    uint32_t id;
};

void *server_handle_conn(void *args_) {
    struct Connection *args = args_;

    printf("CONN(%u): start\n", args->id);

    if (setsockopt(args->socket, SOL_SOCKET, SO_KEEPALIVE, &TCP_KEEPALIVE_ENABLE, sizeof(int)) != 0)
        printf("ERR(server_handle_conn): Enabling socket keepalives on client\n");
    if (setsockopt(args->socket, SOL_TCP, TCP_KEEPIDLE, &TCP_KEEPALIVE_IDLE_TIME, sizeof(int)) != 0)
        printf("ERR(server_handle_conn): Setting initial idle-time value\n");
    if (setsockopt(args->socket, SOL_TCP, TCP_KEEPCNT, &TCP_KEEPALIVE_RETRY_COUNT, sizeof(int)) != 0)
        printf("ERR(server_handle_conn): Setting idle retry count\n");
    if (setsockopt(args->socket, SOL_TCP, TCP_KEEPINTVL, &TCP_KEEPALIVE_RETRY_INTERVAL, sizeof(int)) != 0)
        printf("ERR(server_handle_conn): Setting idle retry interval\n");

    uint8_t buf[2048] __attribute__((aligned(4))) = {};

    PhysicalDevice dev = get_device();
    printf("CONN(%u): got device '%s'\n", args->id, dev.name);

    char *closing_message = "";

    int len = msg_serialize(buf, 2048, (Message *)&dev.device_info);
    if (len > 0) {
        if (write(args->socket, buf, len) == -1) {
            perror("SERVER: Couldn't send device info, ");
            closing_message = "Socket error";
            goto conn_end;
        }
    } else {
        perror("SERVER:  Couldn't serialize device info, ");
        closing_message = "Device info error";
        goto conn_end;
    }

    struct pollfd  pfds[2]     = {};
    struct pollfd *socket_poll = &pfds[0];
    struct pollfd *event_poll  = &pfds[1];

    socket_poll->fd     = args->socket;
    socket_poll->events = POLLIN;
    event_poll->fd      = dev.event;
    event_poll->events  = POLLIN;

    MessageDeviceReport report = {};

    report.code      = DeviceReport;
    report.abs_count = dev.device_info.abs_count;
    report.rel_count = dev.device_info.rel_count;
    report.key_count = dev.device_info.key_count;

    while (1) {
        int rc = poll(pfds, 2, -1);
        if (rc < 0) { // error (connection closed)
            closing_message = "Poll error";
            goto conn_end;
        }

        // Shutdown connection if we lost the peer
        if (socket_poll->revents & POLLHUP || socket_poll->revents & POLLERR) {
            closing_message = "Lost peer";
            goto conn_end;
        }

        if (socket_poll->revents & POLLIN) {
            int len = recv(args->socket, buf, 2048, 0);

            if (len <= 0) {
                closing_message = "Lost peer";
                goto conn_end;
            }

            Message msg;
            if (msg_deserialize(buf, len, &msg) == 0) {

                if (msg.code == ControllerState) {
                    apply_controller_state(&dev, (MessageControllerState *)&msg);
                } else {
                    printf("CONN(%d): Illegal message\n", args->id);
                }

            } else {
                printf("CONN(%d): Couldn't parse message.\n", args->id);
            }
        }

        // Shutdown connection if we lost the device
        if (event_poll->revents & POLLHUP || event_poll->revents & POLLERR) {
            closing_message = "Lost device";
            goto conn_end;
        }

        if (event_poll->revents & POLLIN) {
            struct input_event event;

            int len = read(dev.event, &event, sizeof(struct input_event));

            if (len <= 0) {
                closing_message = "Lost device";
                goto conn_end;
            }

            if (len < sizeof(struct input_event)) {
                printf("CONN(%d): error reading event\n", args->id);
                continue;
            }

            if (event.type == EV_SYN) {
                int len = msg_serialize(buf, 2048, (Message *)&report);

                if (len < 0) {
                    printf("CONN(%d): Couldn't serialize report %d\n", args->id, len);
                    continue;
                };

                write(args->socket, buf, len);
            } else if (event.type == EV_ABS) {
                int index = dev.mapping.abs_indices[event.code];

                if (index < 0) {
                    printf("CONN(%d): Invalid abs\n", args->id);
                    continue;
                };

                report.abs[index] = event.value;
            } else if (event.type == EV_REL) {
                int index = dev.mapping.rel_indices[event.code];

                if (index < 0) {
                    printf("CONN(%d): Invalid rel\n", args->id);
                    continue;
                };

                report.rel[index] = event.value;
            } else if (event.type == EV_KEY) {
                int index = dev.mapping.key_indices[event.code];

                if (index < 0) {
                    printf("CONN(%d): Invalid key\n", args->id);
                    continue;
                };
                report.key[index] = !!event.value;
            }
        }
    }

conn_end:
    shutdown(args->socket, SHUT_RDWR);
    printf("CONN(%u): connection closed (%s)\n", args->id, closing_message);
    return_device(&dev);
    free(args);
    return NULL;
}

void server_run(uint16_t port) {
    printf("SERVER:  start\n");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        panicf("Couldn't open socket\n");
    }

    struct sockaddr_in addr = {};
    addr.sin_family         = AF_INET;
    addr.sin_addr.s_addr    = htonl(INADDR_ANY);
    addr.sin_port           = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        panicf("Couldn't bind to the socket\n");
    }

    if (listen(sock, 16) != 0) {
        panicf("Couldn't listen on socket\n");
    }

    uint32_t ids = 0;
    while (1) {
        struct sockaddr   con_addr;
        socklen_t         con_len = sizeof(con_addr);
        struct Connection conn;

        conn.socket = accept(sock, &con_addr, &con_len);

        if (conn.socket >= 0) {
            printf("SERVER:  got connection\n");

            conn.id = ids++;

            struct Connection *conn_ptr = malloc(sizeof(struct Connection));
            memcpy(conn_ptr, &conn, sizeof(struct Connection));

            pthread_t thread;
            pthread_create(&thread, NULL, server_handle_conn, conn_ptr);
        } else {
            printf("Couldn't accept connection (%d)\n", conn.socket);
        }
    }
}
