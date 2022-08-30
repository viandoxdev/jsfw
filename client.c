#include "client.h"

#include "net.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int               fd;
    MessageDeviceInfo info;
} VirtualDevice;

#ifdef JSFW_DEV
// Path for dev environment (no root)
const char *FIFO_PATH = "/tmp/jsfw_fifo";
#else
const char *FIFO_PATH = "/run/jsfw_fifo";
#endif
const int CONN_RETRY_DELAY = 5;
// Constant for the virtual device
const uint16_t VIRT_VENDOR  = 0x6969;
const uint16_t VIRT_PRODUCT = 0x0420;
const uint16_t VIRT_VERSION = 1;
const char    *VIRT_NAME    = "JSFW Virtual Device";

static int fifo_attempt = 0;

static struct sockaddr_in server_addr      = {};
static char               server_addrp[64] = {};
static uint16_t           server_port      = -1;

static struct pollfd  poll_fds[2];
static struct pollfd *fifo_poll   = &poll_fds[0];
static struct pollfd *socket_poll = &poll_fds[1];
static int            fifo        = -1;
static int            sock        = -1;
// static to avoid having this on the stack because a message is about 2kb in memory
static Message       message;
static VirtualDevice device = {};

static inline bool device_exists() { return device.fd > 0; }

void device_destroy() {
    if (!device_exists()) {
        return;
    }

    ioctl(device.fd, UI_DEV_DESTROY);
    close(device.fd);
    device.fd = -1;
    printf("CLIENT: Destroyed device\n");
}

void device_init(MessageDeviceInfo *dev) {
    device_destroy();

    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("CLIENT: Error while opening /dev/uinput, ");
        exit(1);
    }

    // Abs
    if (dev->abs_count > 0) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        for (int i = 0; i < dev->abs_count; i++) {
            struct uinput_abs_setup setup;
            setup.code               = dev->abs_id[i];
            setup.absinfo.minimum    = dev->abs_min[i];
            setup.absinfo.maximum    = dev->abs_max[i];
            setup.absinfo.fuzz       = dev->abs_fuzz[i];
            setup.absinfo.flat       = dev->abs_flat[i];
            setup.absinfo.resolution = dev->abs_res[i];
            setup.absinfo.value      = 0;
            ioctl(fd, UI_ABS_SETUP, &setup);
        }
    }

    // Rel
    if (dev->rel_count > 0) {
        ioctl(fd, UI_SET_EVBIT, EV_REL);
        for (int i = 0; i < dev->rel_count; i++) {
            ioctl(fd, UI_SET_RELBIT, dev->rel_id[i]);
        }
    }

    // Key
    if (dev->key_count > 0) {
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (int i = 0; i < dev->key_count; i++) {
            ioctl(fd, UI_SET_KEYBIT, dev->key_id[i]);
        }
    }

    struct uinput_setup setup = {};

    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = VIRT_VENDOR;
    setup.id.product = VIRT_PRODUCT;
    setup.id.version = VIRT_VERSION;
    strncpy(setup.name, VIRT_NAME, UINPUT_MAX_NAME_SIZE);

    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);

    device.fd = fd;
    memcpy(&device.info, dev, sizeof(MessageDeviceInfo));
    printf("CLIENT: Created device (abs: %d, rel: %d, key: %d)\n", dev->abs_count, dev->rel_count,
           dev->key_count);
}

int device_emit(uint16_t type, uint16_t id, uint32_t value) {
    struct input_event event = {};

    event.type  = type;
    event.code  = id;
    event.value = value;

    return write(device.fd, &event, sizeof(event)) != sizeof(event);
}

void device_handle_report(MessageDeviceReport *report) {
    if (!device_exists()) {
        printf("CLIENT: Got report but device info\n");
        return;
    }

    if (report->abs_count != device.info.abs_count || report->rel_count != device.info.rel_count ||
        report->key_count != device.info.key_count) {
        printf("CLIENT: Report doesn't match with device info\n");
        return;
    }

    for (int i = 0; i < report->abs_count; i++) {
        if (device_emit(EV_ABS, device.info.abs_id[i], report->abs[i]) != 0)
            printf("CLIENT: Error writing abs event to uinput\n");
    }

    for (int i = 0; i < report->rel_count; i++) {
        if (device_emit(EV_REL, device.info.rel_id[i], report->rel[i]) != 0)
            printf("CLIENT: Error writing rel event to uinput\n");
    }

    for (int i = 0; i < report->key_count; i++) {
        if (device_emit(EV_KEY, device.info.key_id[i], (uint32_t)(!report->key[i]) - 1 ) != 0)
            printf("CLIENT: Error writing key event to uinput\n");
    }

    device_emit(EV_SYN, 0, 0);
}

void setup_fifo();

void open_fifo() {
    close(fifo);
    fifo = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fifo < 0 && fifo_attempt == 0) {
        fifo_attempt++;
        unlink(FIFO_PATH);
        setup_fifo();
    } else if (fifo < 0) {
        panicf("CLIENT: Couldn't open fifo, aborting\n");
    }
}

void setup_fifo() {
    mode_t prev = umask(0);
    mkfifo(FIFO_PATH, 0666);
    umask(prev);

    open_fifo();

    fifo_poll->fd     = fifo;
    fifo_poll->events = POLLIN;
}

void connect_server() {
    while (1) {
        if (sock > 0) {
            device_destroy();
            shutdown(sock, SHUT_RDWR);
            close(sock);
        }

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
            panicf("Couldn't create socket\n");

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
            printf("CLIENT: Couldn't connect to %s:%d, retrying in %ds\n", server_addrp, server_port,
                   CONN_RETRY_DELAY);
            struct timespec ts = {};
            ts.tv_sec          = CONN_RETRY_DELAY;
            nanosleep(&ts, NULL);
            continue;
        }
        // Set non blocking
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        socket_poll->fd = sock;
        printf("CLIENT: Connected !\n");
        return;
    }
}

void setup_server(char *address, uint16_t port) {
    // setup address
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, address, &server_addr.sin_addr) == 0)
        printf("CLIENT: failed to parse address '%s', defaulting to 0.0.0.0 (localhost)\n", address);
    inet_ntop(AF_INET, &server_addr.sin_addr, server_addrp, 64);
    server_port          = port;
    server_addr.sin_port = htons(port);

    socket_poll->events = POLLIN;

    connect_server();
}

void early_checks() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("CLIENT: Can't open /dev/uinput, aborting now: ");
        exit(1);
    }
    close(fd);
}

void client_run(char *address, uint16_t port) {
    // Device doesn't exist yet
    device.fd = -1;

    early_checks();
    setup_fifo();
    setup_server(address, port);

    uint8_t buf[2049];
    while (1) {
        int rc = poll(poll_fds, 2, -1);
        if (rc < 0) {
            perror("CLIENT: Error on poll, ");
            exit(1);
        }

        if (fifo_poll->revents & POLLHUP || fifo_poll->revents & POLLERR) {
            // Reopen fifo
            open_fifo();
        } else if (fifo_poll->revents & POLLIN) {
            int len = read(fifo, buf, 2048);
            if (len <= 0) {
                // This shouldn't ever happen as the poll already checks for the kind of error that would
                // cause len to be <= 0
                printf("CLIENT: supposedly unreachable code reached\n");
                open_fifo();
            } else {
                // We've got data from the fifo
                // TODO: parse and handle that
                buf[len] = '\0';
                printf("CLIENT: Got fifo message:\n%s\n", buf);
            }
        }

        // A broken or closed socket produces a POLLIN event, so we check for error on the recv
        if (socket_poll->revents & POLLIN) {
            int len = recv(sock, buf, 2048, 0);
            if (len <= 0) {
                printf("CLIENT: Lost connection to server, reconnecting\n");
                shutdown(sock, SHUT_RDWR);
                connect_server();
                // we can continue here because there's nothing after, unlike above for fifo (this reduces
                // indentation)
                continue;
            }

            // We've got data from the server
            if (msg_deserialize(buf, len, &message) != 0) {
                printf("CLIENT: Couldn't parse message (code: %d, len: %d)\n", buf[0], len);
                int l = len > 100 ? 100 : len;
                for(int i = 0; i < l; i++) {
                    printf("%02x", buf[i]);
                }
                if(len > 100) {
                    printf(" ... %d more bytes", len - 100);
                }
                printf("\n");
                continue;
            }

            if (message.code == DeviceInfo) {

                if (device_exists())
                    printf("CLIENT: Got more than one device info\n");
                device_init((MessageDeviceInfo *)&message);

            } else if (message.code == DeviceReport) {

                device_handle_report((MessageDeviceReport *)&message);

            } else {
                printf("CLIENT: Illegal message\n");
            }
        }
    }
}
