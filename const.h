// vi:ft=c
#ifndef CONST_H_
#define CONST_H_
#include <stdint.h>
#include <time.h>

#ifndef _FSROOT
#define FSROOT ""
#else
#define __str(a) #a
#define _str(a) __str(a)
#define FSROOT _str(_FSROOT)
#endif

extern const struct timespec POLL_DEVICE_INTERVAL;
extern const int             REQUEST_TIMEOUT;
extern const char           *DEVICE_DEFAULT_NAME;
extern const char           *FIFO_PATH;
extern const struct timespec CONNECTION_RETRY_DELAY;
extern const uint16_t        VIRTUAL_DEVICE_VENDOR;
extern const uint16_t        VIRTUAL_DEVICE_PRODUCT;
extern const uint16_t        VIRTUAL_DEVICE_VERSION;
extern const char           *VIRTUAL_DEVICE_NAME;
extern const int             TCP_KEEPALIVE_ENABLE;
extern const int             TCP_KEEPALIVE_IDLE_TIME;
extern const int             TCP_KEEPALIVE_RETRY_COUNT;
extern const int             TCP_KEEPALIVE_RETRY_INTERVAL;

#endif
