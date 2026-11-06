/* Software (bit-banged) UART TX, 57600 8N1, on PIN_UART_TX (P8).
 *
 * Why not the hardware UART0: on this board UART0 never asserts TXDRDY
 * even with correct PSEL/BAUD/CONFIG/ENABLE read-backs and a directly
 * triggered TASKS_STARTTX (verified over SWD; the original dumped
 * firmware has no UART0 access either). The bit-banged version below is
 * proven working: MINIMAL-2 prints clean text on COM16 at 57600.
 *
 * Timing: driven by a TIMER compare channel with SHORTS=COMPARE0_CLEAR,
 * so bit edges sit on an absolute grid of 278 ticks at 16 MHz
 * (16e6/57600 = 277.78, i.e. +0.08% baud error). Hardware generates the
 * grid, so software latency cannot accumulate into baud drift.
 *
 * Requires app_timer_init() first. Default timer is TIMER_APP_BASE; an
 * image that needs a different instance can define SWUART_TIMER_BASE.
 */
#ifndef SWUART_H
#define SWUART_H

#include "nrf51_min.h"

#ifndef SWUART_TIMER_BASE
#define SWUART_TIMER_BASE TIMER_APP_BASE
#endif

#define SWUART_BIT_TICKS 278UL

static inline void swuart_init(void) {
    gpio_dir_output(PIN_UART_TX);   /* P8 */
    gpio_write(PIN_UART_TX, 1);     /* UART idle = HIGH */
    REG(SWUART_TIMER_BASE, TIMER0_SHORTS) = TIMER_SHORT_COMPARE0_CLEAR;
    REG(SWUART_TIMER_BASE, TIMER0_CC(0)) = SWUART_BIT_TICKS;
    REG(SWUART_TIMER_BASE, TIMER0_EVENTS_COMPARE(0)) = 0UL;
    REG(SWUART_TIMER_BASE, TIMER0_TASKS_CLEAR) = 1UL;
}

/* Wait for the next point on the 278-tick bit grid. */
static inline void swuart_bit_wait(void) {
    REG(SWUART_TIMER_BASE, TIMER0_CC(0)) = SWUART_BIT_TICKS;
    REG(SWUART_TIMER_BASE, TIMER0_EVENTS_COMPARE(0)) = 0UL;
    while (REG(SWUART_TIMER_BASE, TIMER0_EVENTS_COMPARE(0)) == 0UL) {
        __asm__ volatile ("nop");
    }
}

static inline void swuart_putc(char c) {
    /* Re-phase onto the bit grid: delay_ms() leaves CC[0] at a large
     * compare value and the counter may be anywhere. */
    REG(SWUART_TIMER_BASE, TIMER0_CC(0)) = SWUART_BIT_TICKS;
    REG(SWUART_TIMER_BASE, TIMER0_TASKS_CLEAR) = 1UL;
    REG(SWUART_TIMER_BASE, TIMER0_EVENTS_COMPARE(0)) = 0UL;
    gpio_write(PIN_UART_TX, 0);     /* start bit */
    swuart_bit_wait();
    for (int i = 0; i < 8; i++) {   /* data, LSB first */
        gpio_write(PIN_UART_TX, ((uint32_t)c >> i) & 1UL);
        swuart_bit_wait();
    }
    gpio_write(PIN_UART_TX, 1);     /* stop bit */
    swuart_bit_wait();
}

static inline void swuart_puts(const char *s) {
    char prev = 0;
    while (*s) {
        if (*s == '\n' && prev != '\r') swuart_putc('\r');
        swuart_putc(*s);
        prev = *s++;
    }
}

static inline void swuart_put_u32(uint32_t v) {
    char buf[12];
    int i = 11;
    buf[i] = 0;
    if (v == 0) buf[--i] = '0';
    while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    while (buf[i]) swuart_putc(buf[i++]);
}

static inline void swuart_put_hex32(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    for (int sh = 28; sh >= 0; sh -= 4) swuart_putc(hx[(v >> sh) & 0xFUL]);
}

#endif
/* swuart.h */
