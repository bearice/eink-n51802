/* Hardware UART0 TX (polled), 57600 8N1, on PIN_UART_TX (P8).
 *
 * The earlier "hardware UART0 is broken on this board" conclusion was a
 * bug in this project's own register writes, not in the silicon:
 *   - nRF51 UART ENABLE: the field sits at bits [2:0] and the *enabled*
 *     value is 4 (0x04 = "Enabled"), not 1. Writing 1 leaves the UART
 *     disabled, so TXD is never driven and EVENTS_TXDRDY never fires.
 *     Verified: nrf51_bitfields.h UART_ENABLE_ENABLE_Enabled = 0x04.
 *   - nRF51 BAUDRATE is NOT the nRF52 encoding. 57600 baud is
 *     0x00EBF000 (nRF52's 0x00B00000 value does not exist here).
 *     Verified: nrf51_bitfields.h UART_BAUDRATE_BAUDRATE_Baud57600.
 *
 * Hardware beats the bit-banged version for the BLE application: once the
 * SoftDevice is enabled its radio/RTC interrupts preempt the CPU for far
 * longer than one bit time, which corrupts a software UART. TX with the
 * UART peripheral is immune to that.
 */
#ifndef HWUART_H
#define HWUART_H

#include "nrf51_min.h"

#define HWUART_BAUD_57600 0x00EBF000UL

static inline void hwuart_init(void) {
    REG(UART0_BASE, UART0_PSEL_TXD) = PIN_UART_TX;
    REG(UART0_BASE, UART0_PSEL_RXD) = PIN_DISCONNECTED;
    REG(UART0_BASE, UART0_PSEL_CTS) = PIN_DISCONNECTED;
    REG(UART0_BASE, UART0_PSEL_RTS) = PIN_DISCONNECTED;
    REG(UART0_BASE, UART0_BAUDRATE) = HWUART_BAUD_57600;
    REG(UART0_BASE, UART0_CONFIG) = 0UL;              /* 8N1, no parity  */
    REG(UART0_BASE, UART0_ENABLE) = UART_ENABLE_ENABLED;   /* 0x04!      */
}

static inline void hwuart_putc(char c) {
    REG(UART0_BASE, UART0_TXD) = (uint32_t)(uint8_t)c;
    REG(UART0_BASE, UART0_TASKS_STARTTX) = 1UL;
    while (REG(UART0_BASE, UART0_EVENTS_TXDRDY) == 0UL) { }
    REG(UART0_BASE, UART0_EVENTS_TXDRDY) = 0UL;
}

static inline void hwuart_puts(const char *s) {
    char prev = 0;
    while (*s) {
        if (*s == '\n' && prev != '\r') hwuart_putc('\r');
        hwuart_putc(*s);
        prev = *s++;
    }
}

static inline void hwuart_put_u32(uint32_t v) {
    char buf[12];
    int i = 11;
    buf[i] = 0;
    if (v == 0) buf[--i] = '0';
    while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    while (buf[i]) hwuart_putc(buf[i++]);
}

static inline void hwuart_put_hex32(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    for (int sh = 28; sh >= 0; sh -= 4) hwuart_putc(hx[(v >> sh) & 0xFUL]);
}

#endif
/* hwuart.h */
