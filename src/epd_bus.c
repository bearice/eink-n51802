/* Shared e-paper bus: bit-banged 4-wire SPI (mode 0, MSB first) plus the
 * control lines every supported panel uses on this board.
 *
 * Pin order comes from the board wiring (SoC IO in order to the panel
 * FPC), recovered from the stock firmware: P10=DIN, P11=CK, P12=CS,
 * P13=DC, P14=RST, P15=BUSY (not read by the stock firmware), P16=BS1.
 *
 * BUSY is not trustworthy: the stock firmware never configured P15 and
 * the original 4.2" panel's BUSY was not routed. Every wait therefore
 * has a timeout and a probe that gives up immediately when the pin never
 * moves, so the drivers fall back to their fixed delays.
 */
#include "panel.h"
#include "nrf51_min.h"

/* How long to watch BUSY before deciding the pin is not connected. */
#define EPD_BUSY_PROBE_MS 300

volatile uint32_t epd_busy_samples;
volatile uint32_t epd_busy_first_ms;
volatile uint32_t epd_busy_release_ms;
volatile uint32_t epd_busy_used;

void epd_bus_init(void) {
    gpio_dir_output(PIN_EPD_IO);
    gpio_dir_output(PIN_EPD_CK);
    gpio_dir_output(PIN_EPD_CS);
    gpio_dir_output(PIN_EPD_DC);
    gpio_dir_output(PIN_EPD_RST);
    gpio_dir_output(PIN_EPD_BS1);
    gpio_write(PIN_EPD_CK, 0);              /* idle low (mode 0)  */
    gpio_write(PIN_EPD_CS, 1);              /* deasserted         */
    gpio_write(PIN_EPD_DC, 0);
    gpio_write(PIN_EPD_RST, 1);
    gpio_write(PIN_EPD_IO, 0);
    gpio_write(PIN_EPD_BS1, 1);             /* idle               */
    gpio_dir_input(PIN_EPD_BUSY, CFG_INPUT_NOPULL);
}

void epd_bs1(int active) {
    /* BS1 selects the host interface. The stock firmware holds it LOW
     * while it talks to the panel and HIGH at idle, and the board has a
     * separate DC line, i.e. it runs 4-wire SPI. */
    gpio_write(PIN_EPD_BS1, active ? 0 : 1);
}

void epd_all_idle(void) {
    gpio_write(PIN_EPD_IO, 1);
    gpio_write(PIN_EPD_CK, 1);
    gpio_write(PIN_EPD_CS, 1);
    gpio_write(PIN_EPD_DC, 1);
    gpio_write(PIN_EPD_RST, 1);
    epd_bs1(0);
}

static void spi_bit(uint32_t bit) {
    /* Mode 0, MSB first: drive the data line while CLK is low, then raise
     * CLK. This is exactly the stock firmware's bit loop (FUN_00000ac4):
     * P11 low, set P10, P11 high - with no inter-edge delays. */
    gpio_write(PIN_EPD_CK, 0);
    gpio_write(PIN_EPD_IO, bit);
    gpio_write(PIN_EPD_CK, 1);
}

void epd_spi_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) spi_bit((b >> i) & 1);
}

void epd_cmd(uint8_t c) {
    gpio_write(PIN_EPD_DC, 0);
    gpio_write(PIN_EPD_CS, 0);
    epd_spi_byte(c);
    gpio_write(PIN_EPD_CS, 1);
}

void epd_dat(uint8_t d) {
    gpio_write(PIN_EPD_DC, 1);
    gpio_write(PIN_EPD_CS, 0);
    epd_spi_byte(d);
    gpio_write(PIN_EPD_CS, 1);
}

void epd_reset_pulse(uint32_t settle_ms) {
    gpio_write(PIN_EPD_RST, 1);
    delay_ms(20);
    gpio_write(PIN_EPD_RST, 0);
    delay_ms(2);
    gpio_write(PIN_EPD_RST, 1);
    delay_ms(settle_ms);
    gpio_write(PIN_EPD_CS, 1);
}

/* Raw BUSY check, for callers that want to poll instead of block. */
int epd_busy_asserted(int active_high) {
    int level = gpio_read(PIN_EPD_BUSY) ? 1 : 0;
    return level == (active_high ? 1 : 0);
}

/* Wait for the panel. Returns 1 when BUSY showed a real busy pulse, 0
 * when it looks unconnected (the caller then uses its fixed delay). */
int epd_wait_busy(int busy_high, uint32_t timeout_ms) {
    uint32_t t0 = rtc_ms();
    uint32_t seen = 0;
    uint32_t first = 0xFFFFFFFFUL;
    uint32_t release = 0xFFFFFFFFUL;
    uint32_t samples = 0;

    for (;;) {
        int level = gpio_read(PIN_EPD_BUSY) ? 1 : 0;
        int busy = (level != 0) == (busy_high != 0);
        if (busy) {
            if (!seen) first = rtc_ms() - t0;
            seen = 1;
            samples++;
        } else if (seen) {
            release = rtc_ms() - t0;
            break;
        }
        if ((rtc_ms() - t0) >= timeout_ms) break;
        if (!seen && (rtc_ms() - t0) >= EPD_BUSY_PROBE_MS) break;
        delay_ms(10);
    }

    epd_busy_samples = samples;
    epd_busy_first_ms = first;
    epd_busy_release_ms = release;
    epd_busy_used = seen;
    return (int)seen;
}
