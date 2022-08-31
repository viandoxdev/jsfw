#include "const.h"

#include <stdint.h>
#include <time.h>

// How long between each device poll
const struct timespec POLL_DEVICE_INTERVAL = {.tv_sec = 1, .tv_nsec = 0};
// Default name for physical device, only visible in logs
const char *DEVICE_DEFAULT_NAME = "Unnamed Device";
// Path to the fifo
const char *FIFO_PATH = "/tmp/jsfw_fifo";
// Delay (in seconds) between each connection retry for the client
const uint32_t CONNECTION_RETRY_DELAY = 5;
// Displayed bendor for the virtual device
const uint16_t VIRTUAL_DEVICE_VENDOR = 0x6969;
// Displayed product for the virtual device
const uint16_t VIRTUAL_DEVICE_PRODUCT = 0x0420;
// Displayed version for the virtual device
const uint16_t VIRTUAL_DEVICE_VERSION = 1;
// Displayed name for the virtual device
const char *VIRTUAL_DEVICE_NAME = "JSFW Virtual Device";
// Wether to enable keepalive on the tcp spcket
const int TCP_KEEPALIVE_ENABLE = 1;
// How much idle time before sending probe packets
const int TCP_KEEPALIVE_IDLE_TIME = 10;
// How many probes before giving up
const int TCP_KEEPALIVE_RETRY_COUNT = 5;
// How long (in seconds) between each probes
const int TCP_KEEPALIVE_RETRY_INTERVAL = 2;
