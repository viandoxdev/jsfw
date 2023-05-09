#include "client.h"

#include "const.h"
#include "json.h"
#include "net.h"
#include "util.h"
#include "vec.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fifo_attempt = 0;

static struct sockaddr_in server_addr      = {0};
static char               server_addrp[64] = {0};
static uint16_t           server_port      = -1;

static struct pollfd  poll_fds[2];
static struct pollfd *fifo_poll   = &poll_fds[0];
static struct pollfd *socket_poll = &poll_fds[1];
static int            fifo        = -1;
static int            sock        = -1;
// static to avoid having this on the stack because a message is about 2kb in memory
static DeviceMessage message;

static Vec devices_fd;
static Vec devices_info;

static ClientConfig  config;
static DeviceRequest device_request;

static void default_fifo_path(void *ptr) { *(char **)ptr = (char *)FIFO_PATH; }
static void default_retry_delay(void *ptr) { *(struct timespec *)ptr = CONNECTION_RETRY_DELAY; }
static void default_vendor(void *ptr) { *(int32_t *)ptr = VIRTUAL_DEVICE_VENDOR; }
static void default_product(void *ptr) { *(int32_t *)ptr = VIRTUAL_DEVICE_PRODUCT; }
static void default_name(void *ptr) { *(char **)ptr = (char *)VIRTUAL_DEVICE_NAME; }
static void default_to_white(void *ptr) {
    uint8_t *color = ptr;
    color[0]       = 255;
    color[1]       = 255;
    color[2]       = 255;
}

static const JSONPropertyAdapter ControllerStateAdapterProps[] = {
    {".led_color", &StringAdapter, offsetof(DeviceControllerState, led),          default_to_white,    tsf_hex_to_color   },
    {".rumble.0",  &NumberAdapter, offsetof(DeviceControllerState, small_rumble), default_to_zero_u8,  tsf_num_to_u8_clamp},
    {".rumble.1",  &NumberAdapter, offsetof(DeviceControllerState, big_rumble),   default_to_zero_u8,  tsf_num_to_u8_clamp},
    {".flash.0",   &NumberAdapter, offsetof(DeviceControllerState, flash_on),     default_to_zero_u8,  tsf_num_to_u8_clamp},
    {".flash.1",   &NumberAdapter, offsetof(DeviceControllerState, flash_off),    default_to_zero_u8,  tsf_num_to_u8_clamp},
    {".index",     &NumberAdapter, offsetof(DeviceControllerState, index),        default_to_zero_u32, tsf_num_to_int     }
};
static const JSONAdapter ControllerStateAdapter = {
    .props      = (JSONPropertyAdapter *)ControllerStateAdapterProps,
    .prop_count = sizeof(ControllerStateAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(DeviceControllerState),
};

static const JSONPropertyAdapter ControllerAdapterProps[] = {
    {".tag",     &StringAdapter, offsetof(ClientController, tag),            default_to_null, NULL          },
    {".vendor",  &StringAdapter, offsetof(ClientController, device_vendor),  default_vendor,  tsf_hex_to_i32},
    {".product", &StringAdapter, offsetof(ClientController, device_product), default_product, tsf_hex_to_i32},
    {".name",    &StringAdapter, offsetof(ClientController, device_name),    default_name,    NULL          },
};
static const JSONAdapter ControllerAdapter = {
    .props      = ControllerAdapterProps,
    .prop_count = sizeof(ControllerAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(ClientController),
};

static const JSONPropertyAdapter SlotAdapterProps[] = {
    {".controllers[]", &ControllerAdapter, offsetof(ClientSlot, controllers), default_to_null, NULL},
};
static const JSONAdapter SlotAdapter = {
    .props      = SlotAdapterProps,
    .prop_count = sizeof(SlotAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(ClientSlot),
};

static const JSONPropertyAdapter ClientConfigAdapterProps[] = {
    {".slots[]",     &SlotAdapter,   offsetof(ClientConfig, slots),       default_to_null,     NULL                  },
    {".fifo_path",   &StringAdapter, offsetof(ClientConfig, fifo_path),   default_fifo_path,   NULL                  },
    {".retry_delay", &NumberAdapter, offsetof(ClientConfig, retry_delay), default_retry_delay, tsf_numsec_to_timespec}
};
static const JSONAdapter ConfigAdapter = {
    .props      = ClientConfigAdapterProps,
    .prop_count = sizeof(ClientConfigAdapterProps) / sizeof(JSONPropertyAdapter),
    .size       = sizeof(ClientConfig),
};

static void print_config() __attribute__((unused));

// Print the current config, for debugging purposes
static void print_config() {
    printf("CLIENT: Config\n");
    printf("  fifo_path: %s\n", config.fifo_path);
    printf("  retry_delay: %fs\n", timespec_to_double(&config.retry_delay));
    printf("  slots: \n");
    for (size_t i = 0; i < config.slot_count; i++) {
        ClientSlot *slot = &config.slots[i];
        printf("  - controllers:\n");
        for (size_t j = 0; j < slot->controller_count; j++) {
            ClientController *ctr = &slot->controllers[j];
            printf("    - tag: '%s'\n", ctr->tag);
            printf("      name: %s\n", ctr->device_name);
            printf("      vendor: %04x\n", ctr->device_vendor);
            printf("      product: %04x\n", ctr->device_product);
        }
    }
    printf("\n");
}

void destroy_devices(void) {
    for (int i = 0; i < config.slot_count; i++) {
        int         fd   = *(int *)vec_get(&devices_fd, i);
        DeviceInfo *info = vec_get(&devices_info, i);

        if (info->tag == DeviceTagInfo) {
            ioctl(fd, UI_DEV_DESTROY);
            info->tag = DeviceTagNone;
        }
    }
}

bool device_exists(int index) {
    if (index >= devices_info.len) {
        return false;
    }

    DeviceInfo *info = vec_get(&devices_info, index);
    return info->tag == DeviceTagInfo;
}

void device_destroy(int slot) {
    if (slot >= devices_info.len) {
        return;
    }

    int fd = *(int *)vec_get(&devices_fd, slot);

    DeviceInfo *info = vec_get(&devices_info, slot);

    if (info->tag == DeviceTagInfo) {
        ioctl(fd, UI_DEV_DESTROY);
        info->tag = DeviceTagNone;
    }
}

void device_init(DeviceInfo *dev) {
    if (dev->slot >= devices_info.len) {
        printf("CLIENT: Got wrong device index\n");
        return;
    }

    device_destroy(dev->slot);

    int fd = *(int *)vec_get(&devices_fd, dev->slot);

    // Abs
    if (dev->abs.len > 0) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        for (int i = 0; i < dev->abs.len; i++) {
            struct uinput_abs_setup setup = {0};

            Abs abs                  = dev->abs.data[i];
            setup.code               = abs.id;
            setup.absinfo.minimum    = abs.min;
            setup.absinfo.maximum    = abs.max;
            setup.absinfo.fuzz       = abs.fuzz;
            setup.absinfo.flat       = abs.flat;
            setup.absinfo.resolution = abs.res;
            setup.absinfo.value      = 0;
            ioctl(fd, UI_ABS_SETUP, &setup);
        }
    }

    // Rel
    if (dev->rel.len > 0) {
        ioctl(fd, UI_SET_EVBIT, EV_REL);
        for (int i = 0; i < dev->rel.len; i++) {
            ioctl(fd, UI_SET_RELBIT, dev->rel.data[i].id);
        }
    }

    // Key
    if (dev->key.len > 0) {
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (int i = 0; i < dev->key.len; i++) {
            ioctl(fd, UI_SET_KEYBIT, dev->key.data[i].id);
        }
    }

    ClientController *ctr = &config.slots[dev->slot].controllers[dev->index];

    struct uinput_setup setup = {0};

    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = ctr->device_vendor;
    setup.id.product = ctr->device_product;
    setup.id.version = VIRTUAL_DEVICE_VERSION;
    strncpy(setup.name, ctr->device_name, UINPUT_MAX_NAME_SIZE);

    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);

    DeviceInfo *dst = vec_get(&devices_info, dev->slot);

    memcpy(dst, dev, sizeof(DeviceInfo));
    printf("CLIENT: Got device [%d]: '%s' (abs: %d, rel: %d, key: %d)\n", dev->slot, ctr->device_name, dev->abs.len, dev->rel.len,
           dev->key.len);
}

// Send an event to uinput, device must exist
bool device_emit(int index, uint16_t type, uint16_t id, uint32_t value) {
    if (index >= devices_fd.len) {
        return true;
    }

    int                fd    = *(int *)vec_get(&devices_fd, index);
    struct input_event event = {0};

    event.type  = type;
    event.code  = id;
    event.value = value;

    return write(fd, &event, sizeof(event)) != sizeof(event);
}

// Update device with report
void device_handle_report(DeviceReport *report) {
    if (!device_exists(report->slot)) {
        printf("CLIENT: [%d] Got report before device info\n", report->slot);
        return;
    }

    DeviceInfo *info = vec_get(&devices_info, report->slot);

    if (report->abs.len != info->abs.len || report->rel.len != info->rel.len || report->key.len != info->key.len) {
        printf("CLIENT: Report doesn't match with device info (expected %u/%u/%u, got %u/%u/%u)\n", info->abs.len, info->rel.len,
               info->key.len, report->abs.len, report->rel.len, report->key.len);
        return;
    }

    for (int i = 0; i < report->abs.len; i++) {
        if (device_emit(report->slot, EV_ABS, info->abs.data[i].id, report->abs.data[i]) != 0) {
            printf("CLIENT: Error writing abs event to uinput\n");
        }
    }

    for (int i = 0; i < report->rel.len; i++) {
        if (device_emit(report->slot, EV_REL, info->rel.data[i].id, report->rel.data[i]) != 0) {
            printf("CLIENT: Error writing rel event to uinput\n");
        }
    }

    for (int i = 0; i < report->key.len; i++) {
        if (device_emit(report->slot, EV_KEY, info->key.data[i].id, (uint32_t)(!report->key.data[i]) - 1) != 0) {
            printf("CLIENT: Error writing key event to uinput\n");
        }
    }
    // Reports are sent by the server every time the server receives an EV_SYN from the physical device, so we
    // send one when we receive the report to match
    device_emit(report->slot, EV_SYN, 0, 0);
}

void setup_devices(void) {
    devices_fd   = vec_of(int);
    devices_info = vec_of(DeviceInfo);

    DeviceInfo no_info = {0};
    no_info.tag        = DeviceTagNone;

    for (int i = 0; i < config.slot_count; i++) {
        int fd = open(FSROOT "/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            perror("CLIENT: Can't open /dev/uinput, aborting now");
            exit(1);
        }

        vec_push(&devices_fd, &fd);
        vec_push(&devices_info, &no_info);
    }
}

void setup_fifo(void);

// (Re)Open the fifo
void open_fifo(void) {
    close(fifo);
    fifo = open(config.fifo_path, O_RDONLY | O_NONBLOCK);
    if (fifo < 0 && fifo_attempt == 0) {
        fifo_attempt++;
        unlink(config.fifo_path);
        setup_fifo();
    } else if (fifo < 0) {
        panicf("CLIENT: Couldn't open fifo, aborting\n");
    }
    fifo_attempt = 0;
}

// Ensure the fifo exists and opens it (also setup poll_fd)
void setup_fifo(void) {
    mode_t prev = umask(0);
    mkfifo(config.fifo_path, 0666);
    umask(prev);

    open_fifo();

    fifo_poll->fd     = fifo;
    fifo_poll->events = POLLIN;
}

// (Re)Connect to the server
void connect_server(void) {
    while (true) {
        if (sock > 0) {
            // Close previous connection
            shutdown(sock, SHUT_RDWR);
            destroy_devices();
            close(sock);
        }

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            panicf("Couldn't create socket\n");
        }

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
            printf("CLIENT: Couldn't connect to %s:%d, retrying in %lu.%09lus\n", server_addrp, server_port,
                   config.retry_delay.tv_sec, config.retry_delay.tv_nsec);
            nanosleep(&config.retry_delay, NULL);
            continue;
        }
        // Set non blocking, only do that after connection (instead of with SOCK_NONBLOCK at socket creation)
        // because we want to block on the connection itself
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        socket_poll->fd = sock;
        printf("CLIENT: Connected !\n");

        uint8_t buf[2048] __attribute__((aligned(8))) = {0};

        int len = msg_device_serialize(buf, 2048, (DeviceMessage *)&device_request);
        if (len > 0) {
            if (send(sock, buf, len, 0) > 0) {
                printf("CLIENT: Sent device request\n");
            };
        };

        return;
    }
}

// Setup server address and connects to it (+ setup poll_fd)
void setup_server(char *address, uint16_t port) {
    // setup address
    server_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, address, &server_addr.sin_addr) == 0) {
        printf("CLIENT: failed to parse address '%s', defaulting to 0.0.0.0 (localhost)\n", address);
    }
    inet_ntop(AF_INET, &server_addr.sin_addr, server_addrp, 64);

    server_port          = port;
    server_addr.sin_port = htons(port);

    socket_poll->events = POLLIN;

    connect_server();
}

void build_device_request(void) {
    device_request.tag           = DeviceTagRequest;
    device_request.requests.len  = config.slot_count;
    device_request.requests.data = malloc(config.slot_count * sizeof(TagList));
    for (int i = 0; i < config.slot_count; i++) {
        TagList *list   = &device_request.requests.data[i];
        list->tags.len  = config.slots[i].controller_count;
        list->tags.data = malloc(list->tags.len * sizeof(typeof(*list->tags.data)));

        for (int j = 0; j < list->tags.len; j++) {
            char *name                   = config.slots[i].controllers[j].tag;
            list->tags.data[j].name.len  = strlen(name);
            list->tags.data[j].name.data = name;
        }
    }
}

void client_run(char *address, uint16_t port, char *config_path) {
    // Parse the config
    {
        FILE *configfd = fopen(config_path, "r");
        if (configfd == NULL) {
            perror("CLIENT: Couldn't open config file");
            exit(1);
        }

        char    *cbuf = malloc(8192);
        uint8_t *jbuf = (uint8_t *)cbuf + 4096;

        int len = fread(cbuf, 1, 4096, configfd);
        if (json_parse(cbuf, len, jbuf, 4096) != 0) {
            printf("CLIENT: Couldn't parse config, %s (at index %lu)\n", json_strerr(), json_errloc());
            exit(1);
        }

        json_adapt(jbuf, &ConfigAdapter, &config);

#ifdef VERBOSE
        print_config();
#endif

        free(cbuf);
        fclose(configfd);
    }

    setup_fifo();
    build_device_request();
    setup_devices();
    setup_server(address, port);

    uint8_t buf[2048] __attribute__((aligned(8)));
    uint8_t json_buf[2048] __attribute__((aligned(8)));

    while (true) {
        int rc = poll(poll_fds, 2, -1);
        if (rc < 0) {
            perror("CLIENT: Error on poll");
            exit(1);
        }

        if (fifo_poll->revents & POLLIN || fifo_poll->revents & POLLHUP || fifo_poll->revents & POLLERR) {
            int len = read(fifo, buf, 2048);
            if (len <= 0) {
                open_fifo();
            } else {
                // We've got data from the fifo
                int rc = json_parse((char *)buf, len, json_buf, 2048);
                if (rc < 0) {
                    printf("CLIENT: Error when parsing fifo message as json (%s at index %lu)\n", json_strerr(), json_errloc());
                } else {
                    DeviceControllerState msg;
                    msg.tag = DeviceTagControllerState;
                    json_adapt(json_buf, &ControllerStateAdapter, &msg);

                    int len = msg_device_serialize(buf, 2048, (DeviceMessage *)&msg);
                    if (len > 0) {
                        if (send(sock, buf, len, 0) > 0) {
                            printf("CLIENT: Sent controller state: #%02x%02x%02x flash: (%d, %d) rumble: "
                                   "(%d, %d) -> [%d]\n",
                                   msg.led[0], msg.led[1], msg.led[2], msg.flash_on, msg.flash_off, msg.small_rumble,
                                   msg.big_rumble, msg.index);
                        };
                    };
                }
            }
        }

        // A broken or closed socket produces a POLLIN event, so we check for error on the recv
        if (socket_poll->revents & POLLIN) {
            int len = recv(sock, buf, 2048, MSG_PEEK);
            if (len <= 0) {
                printf("CLIENT: Lost connection to server, reconnecting\n");
                connect_server();
                // we can use continue here because there's nothing after, unlike above for fifo (this reduces
                // indentation instead of needing an else block)
                continue;
            }

            int msg_len = msg_device_deserialize(buf, len, &message);
            // We've got data from the server
            if (msg_len < 0) {
                recv(sock, buf, 2048, 0);
                printf("CLIENT: Couldn't parse message (code: %d, len: %d)\n", buf[4], len);

                int l = len > 100 ? 100 : len;
                for (int i = 0; i < l; i++) {
                    printf("%02x", buf[i]);
                }

                if (len > 100) {
                    printf(" ... %d more bytes", len - 100);
                }

                printf("\n");
                continue;
            }

            recv(sock, buf, msg_len, 0);

            if (message.tag == DeviceTagInfo) {
                if (device_exists(message.info.slot)) {
                    printf("CLIENT: Got more than one device info for same device\n");
                }

                device_init((DeviceInfo *)&message);
            } else if (message.tag == DeviceTagReport) {
                device_handle_report((DeviceReport *)&message);
            } else if (message.tag == DeviceTagDestroy) {
                device_destroy(message.destroy.index);
                printf("CLIENT: Lost device %d\n", message.destroy.index);
            } else {
                printf("CLIENT: Illegal message\n");
            }
        }
    }
}
