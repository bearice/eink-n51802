/* WFT0420CZ15 (4.2", 400x300, black/white/red) - the original panel this
 * firmware was written for. Kept verbatim in behaviour so the 4.2" board
 * keeps working after a HINK-E029A01 has been swapped in on another unit.
 *
 * Two controller batches exist for this panel:
 *
 *   UC8176 ("V1", old batch) - the one on the original board. The full
 *     command sequence was recovered from the stock firmware with Ghidra:
 *     0x04 power on -> 0x00 {0x0F,0x0D} -> 0x10 (black) -> 0x13 (red)
 *     -> 0x12 (refresh) -> 0x50=0xF7 -> 0x02 power off -> 0x07=0xA5.
 *     Its delays are fixed, not BUSY driven: FUN_00000340(n) burns
 *     n*999 nop iterations.
 *
 *   UC8179 ("V2", newer batch) - Waveshare epd4in2b_V2 Init_new():
 *     0x12 swreset, 0x3C border, 0x18 temp sensor, 0x11 data entry,
 *     0x44/0x45 RAM window, planes 0x24 (black) / 0x26 (red, inverted),
 *     refresh 0x22=0xF7 + 0x20, sleep 0x10=0x01.
 *
 * The red plane polarity differs from the 2.9" panels: here 1 = red and
 * the data is sent inverted, which the descriptor's red_inverted flag
 * tells the front end.
 */
#include "panel.h"
#include "nrf51_min.h"

#define WFT_WIDTH   400
#define WFT_HEIGHT  300

static int s_ready;

static void wft_gpio(void) {
    epd_bs1(1);                 /* talking to the panel */
}

/* ---- UC8176 -------------------------------------------------------- */

/* Stock reset (FUN_00000c08): BS1 low, then three RST low/high pulses of
 * ~2 ms each, then a ~180 ms settle. */
static void reset_v1(void) {
    wft_gpio();
    for (int i = 0; i < 3; i++) {
        gpio_write(PIN_EPD_RST, 0);
        delay_ms(2);
        gpio_write(PIN_EPD_RST, 1);
        delay_ms(2);
    }
    delay_ms(180);
}

static void init_v1(void) {
    reset_v1();
    epd_cmd(0x04);              /* power on */
    epd_wait_busy(1, 5000);     /* power-on ramp is ~1 s */
    epd_cmd(0x00);              /* panel setting */
    epd_dat(0x0F);
    epd_dat(0x0D);
    s_ready = 1;
}

static void open_bw_v1(void)  { epd_cmd(0x10); }
static void open_red_v1(void) { epd_cmd(0x13); }

static void refresh_start_v1(void) {
    epd_cmd(0x12);              /* display refresh */
}

static void deep_sleep_v1(void) {
    epd_cmd(0x50);              /* VCOM and data interval */
    epd_dat(0xF7);
    delay_ms(100);
    epd_cmd(0x02);              /* power off */
    delay_ms(300);
    epd_cmd(0x07);              /* deep sleep */
    epd_dat(0xA5);
    delay_ms(300);
    epd_all_idle();
}

static void refresh_finish_v1(void) {
    if (!epd_wait_busy(1, 20000)) delay_ms(20000);
    deep_sleep_v1();
}

/* ---- UC8179 -------------------------------------------------------- */
static void init_v2(void) {
    wft_gpio();
    epd_reset_pulse(20);

    epd_cmd(0x12);              /* software reset */
    delay_ms(10);
    epd_cmd(0x3C);              /* border waveform */
    epd_dat(0x05);
    delay_ms(2);
    epd_cmd(0x18);              /* internal temperature sensor */
    epd_dat(0x80);
    delay_ms(2);
    epd_cmd(0x11);              /* data entry mode */
    epd_dat(0x03);
    delay_ms(2);
    epd_cmd(0x44);              /* X address start/end (400/8-1 = 0x31) */
    epd_dat(0x00);
    epd_dat(0x31);
    epd_cmd(0x45);              /* Y address start/end (299 = 0x012B) */
    epd_dat(0x00);
    epd_dat(0x00);
    epd_dat(0x2B);
    epd_dat(0x01);
    epd_cmd(0x4E);              /* X counter = 0 */
    epd_dat(0x00);
    epd_cmd(0x4F);              /* Y counter = 0 */
    epd_dat(0x00);
    epd_dat(0x00);
    delay_ms(2);
    s_ready = 1;
}

static void open_bw_v2(void)  { epd_cmd(0x24); }
static void open_red_v2(void) { epd_cmd(0x26); }

static void refresh_start_v2(void) {
    epd_cmd(0x22);              /* display update control 2 */
    epd_dat(0xF7);              /* full refresh */
    epd_cmd(0x20);              /* activate */
}

static void deep_sleep_v2(void) {
    epd_cmd(0x10);              /* deep sleep */
    epd_dat(0x01);
    delay_ms(100);
    epd_all_idle();
}

static void refresh_finish_v2(void) {
    if (!epd_wait_busy(1, 25000)) delay_ms(25000);
    deep_sleep_v2();
}

/* Both variants (re)initialise from sleep, so the API mirrors the other
 * panel drivers: init() must be re-runnable. */
static void init_v1_again(void) { s_ready = 0; init_v1(); }
static void init_v2_again(void) { s_ready = 0; init_v2(); }

const panel_t panel_wft_uc8176 = {
    "WFT0420CZ15", "UC8176", WFT_WIDTH, WFT_HEIGHT,
    2, 0, 1, 20000,
    init_v1_again, open_bw_v1, open_red_v1, refresh_start_v1, refresh_finish_v1,
    deep_sleep_v1,
};

const panel_t panel_wft_uc8179 = {
    "WFT0420CZ15", "UC8179", WFT_WIDTH, WFT_HEIGHT,
    2, 0, 1, 25000,
    init_v2_again, open_bw_v2, open_red_v2, refresh_start_v2, refresh_finish_v2,
    deep_sleep_v2,
};
