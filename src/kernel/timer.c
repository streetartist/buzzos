#include "timer.h"
#include "io.h"
#include "serial.h"
#include "task.h"

/* 8254 PIT ports and control values. */
enum {
    PIT_CH0_DATA = 0x40,
    PIT_CMD      = 0x43,
    /* channel 0, lobyte/hibyte access, mode 3 (square wave), binary */
    PIT_CMD_CH0_MODE3 = 0x36,
    PIT_BASE_FREQ     = 1193182u,  /* input clock to the PIT */

    PIC1_DATA = 0x21,
};

static volatile uint32_t ticks;

void timer_init(void) {
    uint32_t divisor = PIT_BASE_FREQ / TIMER_HZ;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    outb(PIT_CMD, PIT_CMD_CH0_MODE3);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    /* Unmask IRQ0 (timer) on the master PIC, leaving other masked lines
     * untouched. The keyboard (IRQ1) was already unmasked in pic_remap. */
    uint8_t mask = inb(PIC1_DATA);
    mask &= (uint8_t)~0x01;
    outb(PIC1_DATA, mask);

    serial_puts("[timer] PIT @ ");
    serial_puthex(TIMER_HZ);
    serial_puts(" Hz, divisor=");
    serial_puthex(divisor);
    serial_puts("\n");
}

void timer_irq(void) {
    ticks++;
    /* Preempt: round-robin to the next ready task. schedule() handles the
     * cli/sti and the no-op case when nothing else is runnable. */
    sched_tick();
}

uint32_t timer_ticks(void) {
    return ticks;
}

uint32_t timer_uptime_secs(void) {
    return ticks / TIMER_HZ;
}

void timer_sleep_ms(uint32_t ms) {
    /* Convert ms → ticks, rounding up so a sub-tick sleep still waits.
     * 32-bit math throughout to avoid libgcc 64-bit division helpers. */
    uint32_t want = (ms / 1000u) * TIMER_HZ
                  + ((ms % 1000u) * TIMER_HZ + 999u) / 1000u;
    uint32_t start = ticks;
    while (ticks - start < want)
        task_yield();
}
