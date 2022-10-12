#include "server.h"

#include "const.h"
#include "hid.h"
#include "json.h"
#include "net.h"
#include "util.h"
#include "vec.h"

#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
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
    bool     closed;
};

struct DeviceThreadArgs {
    int                index;
    char              *tag;
    Controller       **controller;
    struct Connection *conn;
};

static void default_timespec(void *ptr) { *(struct timespec *)ptr = POLL_DEVICE_INTERVAL; }
static void default_request_timeout(void *ptr) { *(uint32_t *)ptr = REQUEST_TIMEOUT; }

const JSONPropertyAdapter FilterAdapterProps[] = {
    {".uniq",    &StringAdapter,  offsetof(ControllerFilter, uniq),    default_to_zero_u64,         tsf_uniq_to_u64},
    {".vendor",  &StringAdapter,  offsetof(ControllerFilter, vendor),  default_to_negative_one_i32, tsf_hex_to_i32 },
    {".product", &StringAdapter,  offsetof(ControllerFilter, product), default_to_negative_one_i32, tsf_hex_to_i32 },
    {".js",      &BooleanAdapter, offsetof(ControllerFilter, js),      default_to_false,            NULL           },
    {".name",    &StringAdapter,  offsetof(ControllerFilter, name),    default_to_null,             NULL           },
};
const JSONAdapter FilterAdapter = {
    .props      = FilterAdapterProps,
    .prop_count = sizeof(FilterAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(ControllerFilter),
};

const JSONPropertyAdapter ControllerAdapterProps[] = {
    {".filter",                &FilterAdapter,  offsetof(ServerConfigController, filter),     NULL,             NULL},
    {".tag",                   &StringAdapter,  offsetof(ServerConfigController, tag),        default_to_null,  NULL},
    {".properties.duplicate",  &BooleanAdapter, offsetof(ServerConfigController, duplicate),  default_to_false, NULL},
    {".properties.ps4_hidraw", &BooleanAdapter, offsetof(ServerConfigController, ps4_hidraw), default_to_false, NULL},
};
const JSONAdapter ControllerAdapter = {
    .props      = ControllerAdapterProps,
    .prop_count = sizeof(ControllerAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(ServerConfigController),
};

const JSONPropertyAdapter ConfigAdapterProps[] = {
    {".controllers[]",   &ControllerAdapter, offsetof(ServerConfig, controllers),     default_to_null,         NULL                  },
    {".poll_interval",   &NumberAdapter,     offsetof(ServerConfig, poll_interval),   default_timespec,        tsf_numsec_to_timespec},
    {".request_timeout", &NumberAdapter,     offsetof(ServerConfig, request_timeout), default_request_timeout, tsf_numsec_to_intms   }
};
const JSONAdapter ConfigAdapter = {
    .props      = ConfigAdapterProps,
    .prop_count = sizeof(ConfigAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(ServerConfig),
};

static ServerConfig  config;
static pthread_key_t device_args_key;
static sigset_t      empty_sigset;

#define TRAP(sig, handler)                                                                                                       \
    if (sigaction(sig, &(struct sigaction){.sa_handler = handler, .sa_mask = empty_sigset, .sa_flags = 0}, NULL) != 0)           \
    printf("SERVER:  can't trap " #sig ".\n")
#define TRAP_IGN(sig)                                                                                                            \
    if (sigaction(sig, &(struct sigaction){{SIG_IGN}}, NULL) != 0)                                                               \
    printf("SERVER:  can't ignore " #sig ".\n")

void device_thread_exit(int _sig) {
    struct DeviceThreadArgs *args = pthread_getspecific(device_args_key);
    printf("CONN(%d): [%d] exiting\n", args->conn->id, args->index);

    Controller *ctr = *args->controller;
    if (ctr != NULL) {
        return_device(ctr);
    }

    free(args->tag);
    free(args);
    pthread_exit(NULL);
}

void *device_thread(void *args_) {
    struct DeviceThreadArgs *args = args_;

    pthread_setspecific(device_args_key, args);

    TRAP_IGN(SIGPIPE);
    TRAP(SIGTERM, device_thread_exit);

    uint8_t           buf[2048] __attribute__((aligned(4))) = {0};
    MessageDeviceInfo dev_info;

    while (true) {
        *args->controller = NULL;
        Controller *ctr   = get_device(args->tag, &args->conn->closed);
        if(ctr == NULL) {
            break;
        }
        *args->controller = ctr;
        dev_info          = ctr->dev.device_info;
        dev_info.index    = args->index;

        printf("CONN(%d): [%d] Found suitable [%s] device: '%s' (%016lx)\n", args->conn->id, args->index, args->tag,
               ctr->dev.name, ctr->dev.id);

        // Send over device info
        {
            int len = msg_serialize(buf, 2048, (Message *)&dev_info);
            if (write(args->conn->socket, buf, len) == -1) {
                printf("CONN(%d): [%d] Couldn't send device info\n", args->conn->id, args->index);
                break;
            }
        }

        MessageDeviceReport report = {0};

        report.code      = DeviceReport;
        report.abs_count = ctr->dev.device_info.abs_count;
        report.rel_count = ctr->dev.device_info.rel_count;
        report.key_count = ctr->dev.device_info.key_count;
        report.index     = args->index;

        while (true) {
            struct input_event event;

            int len = read(ctr->dev.event, &event, sizeof(struct input_event));

            if (len <= 0) {
                // We lost the device, so we mark it as broken (we forget it) and try to get a new one (in the next iteration of
                // the outer while)
                forget_device(ctr);
                break;
            }

            if (len < sizeof(struct input_event)) {
                printf("CONN(%d): [%d] error reading event\n", args->conn->id, args->index);
                continue;
            }

            if (event.type == EV_SYN) {
                int len = msg_serialize(buf, 2048, (Message *)&report);

                if (len < 0) {
                    printf("CONN(%d): [%d] Couldn't serialize report %d\n", args->conn->id, args->index, len);
                    continue;
                };

                send(args->conn->socket, buf, len, 0);
            } else if (event.type == EV_ABS) {
                int index = ctr->dev.mapping.abs_indices[event.code];

                if (index < 0) {
                    printf("CONN(%d): [%d] Invalid abs\n", args->conn->id, args->index);
                    continue;
                };

                report.abs[index] = event.value;
            } else if (event.type == EV_REL) {
                int index = ctr->dev.mapping.rel_indices[event.code];

                if (index < 0) {
                    printf("CONN(%d): [%d] Invalid rel\n", args->conn->id, args->index);
                    continue;
                };

                report.rel[index] = event.value;
            } else if (event.type == EV_KEY) {
                int index = ctr->dev.mapping.key_indices[event.code];

                if (index < 0) {
                    printf("CONN(%d): [%d] Invalid key\n", args->conn->id, args->index);
                    continue;
                };
                report.key[index] = !!event.value;
            }
        }

        // Send device destroy message
        {
            MessageDestroy dstr;
            dstr.code  = DeviceDestroy;
            dstr.index = args->index;

            int len = msg_serialize(buf, 2048, (Message *)&dstr);
            if (write(args->conn->socket, buf, len) == -1) {
                printf("CONN(%d): [%d] Couldn't send device destroy message\n", args->conn->id, args->index);
                break;
            }
        }
    }

    device_thread_exit(SIGTERM);
    return NULL;
}

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

    uint8_t buf[2048] __attribute__((aligned(4))) = {0};

    char *closing_message    = "";
    bool  got_request        = false;
    Vec   device_threads     = vec_of(pthread_t);
    Vec   device_controllers = vec_of(Controller *);

    struct pollfd pfd = {.fd = args->socket, .events = POLLIN};

    while (1) {
        int rc = poll(&pfd, 1, config.request_timeout);

        // If poll timed out
        if (rc == 0) {
            if (!got_request) {
                printf("CONN(%d): Didn't get a device request within %i.%03ds\n", args->id, config.request_timeout / 1000,
                       config.request_timeout % 1000);
                closing_message = "Timed out";
                goto conn_end;
            }

            continue;
        } else if (rc < 0) { // If poll failed (basically never)
            closing_message = "Poll error";
            goto conn_end;
        }

        // Test for error on socket
        if (pfd.revents & POLLHUP || pfd.revents & POLLERR) {
            closing_message = "Lost peer";
            goto conn_end;
        }

        // Receive data
        int len = recv(args->socket, buf, 2048, 0);
        if (len <= 0) {
            closing_message = "Lost peer (from recv)";
            goto conn_end;
        } else if (len > 1 + MAGIC_SIZE * 2) {
            printf("CONN(%d):  Got message: ", args->id);
            printf("\n");
        } else {
            printf("CONN(%d):  Malformed message\n", args->id);
        }

        // Parse message
        Message msg;
        int     msg_len = msg_deserialize(buf, len, &msg);
        if (msg_len < 0) {
            if (len > 1 + MAGIC_SIZE * 2) {
                printf("CONN(%d): Couldn't parse message: ", args->id);
                print_message_buffer(buf, len);
                printf("\n");
            } else {
                printf("CONN(%d): Couldn't parse message", args->id);
            }
            continue;
        }

        // Handle message
        if (msg.code == ControllerState) {
            int i = msg.controller_state.index;
            if (i >= device_controllers.len) {
                printf("CONN(%d): Invalid controller index in controller state message\n", args->id);
                continue;
            }

            Controller *ctr = *(Controller **)vec_get(&device_controllers, i);
            if (ctr == NULL) {
                printf("CONN(%d): Received controller state message but the device hasn't yet been received\n", args->id);
                continue;
            }

            apply_controller_state(ctr, &msg.controller_state);
        } else if (msg.code == Request) {
            if (got_request) {
                printf("CONN(%d): Illegal Request message after initial request\n", args->id);
                msg_free(&msg);
                continue;
            }

            got_request = true;

            printf("CONN(%d): Got client request\n", args->id);

            for (int i = 0; i < msg.request.request_count; i++) {
                int         index = device_controllers.len;
                Controller *ctr   = NULL;
                vec_push(&device_controllers, &ctr);

                struct DeviceThreadArgs *dev_args = malloc(sizeof(struct DeviceThreadArgs));
                dev_args->controller              = vec_get(&device_controllers, index);
                dev_args->tag                     = strdup(msg.request.requests[i]);
                dev_args->conn                    = args;
                dev_args->index                   = index;

                pthread_t thread;
                pthread_create(&thread, NULL, device_thread, dev_args);
                vec_push(&device_threads, &thread);
            }

            msg_free(&msg);
        } else {
            printf("CONN(%d): Illegal message\n", args->id);
        }
    }

conn_end:
    shutdown(args->socket, SHUT_RDWR);
    printf("CONN(%u): connection closed (%s)\n", args->id, closing_message);
    args->closed = true;
    for (int i = 0; i < device_threads.len; i++) {
        pthread_t thread = *(pthread_t *)vec_get(&device_threads, i);
        pthread_kill(thread, SIGTERM);
        pthread_join(thread, NULL);
    }
    free(args);
    return NULL;
}

static int sockfd;

void clean_exit(int _sig) {
    printf("\rSERVER:  exiting\n");
    close(sockfd);
    exit(0);
}

void server_run(uint16_t port, char *config_path) {
    sigemptyset(&empty_sigset);
    pthread_key_create(&device_args_key, NULL);
    printf("SERVER:  start\n");

    // Parse the config
    {
        FILE *configfd = fopen(config_path, "r");
        if (configfd == NULL) {
            perror("SERVER:  Couldn't open config file");
            exit(1);
        }

        char    *cbuf = malloc(8192);
        uint8_t *jbuf = (uint8_t *)cbuf + 4096;

        int len = fread(cbuf, 1, 4096, configfd);
        if (json_parse(cbuf, len, jbuf, 4096) != 0) {
            printf("SERVER:  Couldn't parse config, %s (at index %lu)\n", json_strerr(), json_errloc());
            exit(1);
        }

        json_adapt(jbuf, &ConfigAdapter, &config);

        free(cbuf);
        fclose(configfd);
    }

    // Start the hid thread
    {
        pthread_t _thread;
        pthread_create(&_thread, NULL, hid_thread, &config);
    }

    // Start the TCP server

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        panicf("Couldn't open socket\n");
    }

    sockfd = sock;

    TRAP(SIGINT, clean_exit);
    TRAP(SIGHUP, clean_exit);
    TRAP(SIGQUIT, clean_exit);
    TRAP(SIGTERM, clean_exit);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        printf("SERVER:  error when trying to enable SO_REUSEADDR\n");
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0)
        printf("SERVER:  error when trying to enable SO_REUSEPORT\n");

    struct sockaddr_in addr = {0};
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
        conn.closed = false;

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
