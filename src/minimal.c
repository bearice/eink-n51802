/* Minimal nRF51802 self-test (standalone, runs at 0x0, no SoftDevice).
 *
 * Purpose: prove the chip boots, HFCLK runs, GPIO works (LEDs) and the
 * software UART on P8 gets out. This image is the baseline used to
 * validate the shared timing layer before trusting the full BLE app.
 *
 * Boot sequence:
 *   1. Start HFCLK (poll with ~1 s timeout -> remember timeout flag)
 *   2. App timer up: TIMER0, 16 MHz, 32-bit, MODE=0 (timer, not counter)
 *   3. LEDs P3-P5 outputs, P3 on (active-low: on = 0)
 *   4. Software UART TX=P8, 57600 8N1 -> log clock/timer state + ticks
 *   5. Toggle P3 every 250 ms, print "TICK n" every ~1 s
 *
 * Hardware-verified on this board (2026-11): log text is clean on COM16
 * at 57600 and TICK lines arrive every ~1.006 s, so delay_ms() is right.
 */
#include "nrf51_min.h"
#include "hwuart.h"

static uint32_t g_n;

int main(void) {
    /* 1. HFCLK: start, poll HFCLKSTAT (bit0 = running, bits16- = source).
     *    nRF51802 needs the 16 MHz board crystal for HFCLK; without it
     *    everything timed (UART, SPI, e-ink) is wrong. Log the outcome. */
    uint32_t hfclk_timeout = 0;
    REG(CLOCK_BASE, CLOCK_TASKS_STARTHFCLK) = 1UL;
    {
        uint32_t t = 0;
        while (!(REG(CLOCK_BASE, CLOCK_HFCLKSTAT) & 1UL)) {
            if (++t > 500000UL) { hfclk_timeout = 1; break; }
        }
    }

    /* 2. Time base: LFCLK + RTC1 (ms delays; delay_us is a nop loop) */
    lfclk_start_rc();
    rtc_init();

    /* 3. GPIO: LEDs off; P3 on (self-drive check) */
    gpio_dir_output(PIN_LED1);
    gpio_dir_output(PIN_LED2);
    gpio_dir_output(PIN_LED3);
    gpio_write(PIN_LED1, 1);      /* off (active low) */
    gpio_write(PIN_LED2, 1);
    gpio_write(PIN_LED3, 1);
    gpio_write(PIN_LED1, 0);      /* P3 on */

    /* 4. Software UART on P8 */
    hwuart_init();
    hwuart_puts("\r\nMINIMAL-4 HWUART boot\r\n");
    hwuart_puts("TIMER0 mode=");
    hwuart_put_u32(REG(TIMER0_BASE, TIMER0_MODE));
    hwuart_puts(" bitmode=");
    hwuart_put_u32(REG(TIMER0_BASE, TIMER0_BITMODE));
    hwuart_puts(" presc=");
    hwuart_put_u32(REG(TIMER0_BASE, TIMER0_PRESCALER));
    hwuart_puts("\r\nHFCLKSTAT=0x");
    hwuart_put_hex32(REG(CLOCK_BASE, CLOCK_HFCLKSTAT));
    hwuart_puts(" timeout=");
    hwuart_put_u32(hfclk_timeout);
    hwuart_puts("\r\nRESETREAS=0x");
    hwuart_put_hex32(REG(POWER_BASE, POWER_RESETREAS));
    hwuart_puts("\r\n");

    for (;;) {
        g_n++;
        /* P3 heartbeat 250 ms on/off, P4 toggles each printed tick */
        gpio_write(PIN_LED1, (g_n & 1U) ? 0 : 1);
        gpio_write(PIN_LED2, (g_n & 4U) ? 0 : 1);
        if ((g_n & 3U) == 0U) {
            hwuart_puts("TICK ");
            hwuart_put_u32(g_n);
            hwuart_puts(" HF=");
            hwuart_put_u32(REG(CLOCK_BASE, CLOCK_HFCLKSTAT) & 1UL);
            hwuart_puts(" OUT=0x");
            hwuart_put_hex32(REG(GPIO_BASE, GPIO_OUT));
            hwuart_puts("\r\n");
        }
        delay_ms(250);
    }
    return 0;
}
