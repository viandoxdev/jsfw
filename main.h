#ifndef MAIN_H
#define MAIN_H
#include <stdint.h>

typedef struct {
  char * name;
  uint8_t button_count;
  uint8_t axis_count;
} Joystick;

#endif
