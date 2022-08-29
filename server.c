#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "hid.h"
#include "main.h"
#include "net.h"
#include "vec.h"

struct Connection {
    int      socket;
    uint32_t id;
};

void *server_handle_conn(void *args_) {
    struct Connection *args = args_;

    printf("THREAD(%u): start\n", args->id);

    int enable        = 1;
    int idle_time     = 10;
    int keep_count    = 5;
    int keep_interval = 5;
    if (setsockopt(args->socket, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) != 0)
        printf("ERR(server_handle_conn): Enabling socket keepalives on client\n");
    if (setsockopt(args->socket, SOL_TCP, TCP_KEEPIDLE, &idle_time, sizeof(idle_time)) != 0)
        printf("ERR(server_handle_conn): Setting initial ERR()-time value\n");
    if (setsockopt(args->socket, SOL_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count)) != 0)
        printf("ERR(server_handle_conn): Setting idle retry count\n");
    if (setsockopt(args->socket, SOL_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval)) != 0)
        printf("ERR(server_handle_conn): Setting idle retry interval\n");

    PhysicalDevice dev = get_device();
    printf("THREAD(%u): got device '%s'\n", args->id, dev.name);

    uint8_t buf[1024];
    while (1) {
        int len = recv(args->socket, buf, 1024, MSG_WAITALL);

        if (len <= 0)
            goto conn_end;

        Message msg;
        if (msg_deserialize(buf, len, &msg) == 0) {

        } else {
            printf("Couldn't parse message.\n");
        }
    }
    printf("THREAD(%u): connection closed\n", args->id);

conn_end:
    return_device(&dev);
    free(args);
    return NULL;
}

void server_run(uint16_t port) {
    printf("SERVER: start\n");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        panicf("Couldn't open socket\n");

    struct sockaddr_in addr = {};
    addr.sin_family         = AF_INET;
    addr.sin_addr.s_addr    = htonl(INADDR_ANY);
    addr.sin_port           = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        panicf("Couldn't bind to the socket\n");

    if (listen(sock, 16) != 0)
        panicf("Couldn't listen on socket\n");

    uint32_t ids = 0;
    while (1) {
        struct sockaddr   con_addr;
        socklen_t         con_len = sizeof(con_addr);
        struct Connection conn;

        conn.socket = accept(sock, &con_addr, &con_len);

        if (conn.socket >= 0) {
            printf("SERVER: got connection\n");

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
