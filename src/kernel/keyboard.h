#ifndef BUZZOS_KEYBOARD_H
#define BUZZOS_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
void keyboard_handler(uint8_t scancode);
int  keyboard_getchar(void);   /* blocking — returns ASCII or 0 if empty */

#endif
