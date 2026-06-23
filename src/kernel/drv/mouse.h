#ifndef BUZZOS_MOUSE_H
#define BUZZOS_MOUSE_H

#include <stdint.h>

struct mouse_state {
    int x;
    int y;
    int buttons;
    int dx;
    int dy;
    uint32_t seq;
};

void mouse_init(void);
void mouse_handler(uint8_t byte);
void mouse_get_state(struct mouse_state *out);

#endif /* BUZZOS_MOUSE_H */
