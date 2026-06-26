#ifndef BUZZOS_USER_H
#define BUZZOS_USER_H

#include <stdint.h>
#include "user_bounds.h"

/* Enter ring 3 through a small trampoline that reloads user data segments
 * and jumps to the supplied entry point. Does not return. */
void user_enter(uint32_t entry, uint32_t stack_top) __attribute__((noreturn));

#endif /* BUZZOS_USER_H */
