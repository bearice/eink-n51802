/* HINK-E029A01 (2.9", 296x128, black/white/red) - two controller
 * generations, both supported at run time.
 *
 *   SSD1680 - Waveshare "2.9inch e-Paper (B) V2" sequence: software
 *             reset 0x12, driver output 0x01, data entry 0x11, RAM
 *             window 0x44/0x45, border 0x3C, update control 0x21,
 *             planes 0x24 (black) / 0x26 (red), refresh 0x22=0xF7 then
 *             0x20, sleep 0x10=0x01. BUSY is HIGH while busy.
 *
 *   UC8151  - Waveshare "2.9inch e-Paper (B/C)" sequence: booster 0x06,
 *             power on 0x04, panel setting 0x00=0x8F, VCOM/data 0x50,
 *             TCON resolution 0x61, VCM DC 0x82, planes 0x10 (black) /
 *             0x13 (red) with 0x92 (partial out) between them, refresh
 *             0x12, power off 0x02, deep sleep 0x07=0xA5. BUSY is LOW
 *             while busy - the opposite polarity of the SSD1680.
 *
 * Both write the same RAM shape: 296 gate rows, 16 bytes (128 pixels)
 * per row = 4736 bytes per plane. Bit 1 = white / no ink, bit 0 = ink,
 * in both planes - unlike the 4.2" panel, the red plane is not inverted.
 */
#include "panel.h"
#include "nrf51_min.h"

/* Geometry comes from the panel descriptor, so the same SSD1680 code
 * drives both the 2.9" (128x296) and the 2.13" (122x250) panel. */
/* Measured on the 2.9" part: the full tri-colour update takes ~30.7 s
 * (BUSY high, repeatable), and ~9.3 s was observed earlier in the same
 * session for the same content, so the waveform time is not stable.
 * Use the longer figure for the fixed fallback and for the BUSY timeout
 * so a slow refresh is never truncated by an early deep-sleep command. */
#define HINK_REFRESH_MS 32000
#define HINK_BUSY_TIMEOUT_MS 45000

/* ---- SSD1680 ------------------------------------------------------- */
static void ssd_init(void) {
    const panel_t *p = panel_active();
    const uint32_t gates = p->height;
    const uint32_t rowbytes = panel_row_bytes(p);

    epd_bs1(1);
    epd_reset_pulse(20);

    epd_cmd(0x12);              /* soft reset */
    delay_ms(20);
    epd_wait_busy(1, 2000);

    epd_cmd(0x01);              /* driver output control: gate count */
    epd_dat((uint8_t)((gates - 1) & 0xFF));
    epd_dat((uint8_t)((gates - 1) >> 8));
    epd_dat(0x00);

    epd_cmd(0x11);              /* data entry mode: X+ Y+ */
    epd_dat(0x03);

    epd_cmd(0x44);              /* RAM X start/end, in bytes */
    epd_dat(0x00);
    epd_dat((uint8_t)(rowbytes - 1));

    epd_cmd(0x45);              /* RAM Y start/end, start first */
    epd_dat(0x00);
    epd_dat(0x00);
    epd_dat((uint8_t)((gates - 1) & 0xFF));
    epd_dat((uint8_t)((gates - 1) >> 8));

    epd_cmd(0x3C);              /* border waveform */
    epd_dat(0x05);

    epd_cmd(0x21);              /* display update control 1: load LUT
                                 * from the panel's OTP, normal mode */
    epd_dat(0x00);
    epd_dat(0x80);

    epd_cmd(0x18);              /* built-in temperature sensor */
    epd_dat(0x80);

    epd_cmd(0x4E);              /* RAM X address counter = 0 */
    epd_dat(0x00);
    epd_cmd(0x4F);              /* RAM Y address counter = 0 */
    epd_dat(0x00);
    epd_dat(0x00);

    epd_wait_busy(1, 3000);
}

static void ssd_open_bw(void)  { epd_cmd(0x24); }
static void ssd_open_red(void) { epd_cmd(0x26); }

static void ssd_refresh_start(void) {
    epd_cmd(0x22);              /* display update control 2 */
    epd_dat(0xF7);              /* full update */
    epd_cmd(0x20);              /* master activation */
}

static void ssd_deep_sleep(void) {
    epd_cmd(0x10);              /* deep sleep mode 1 */
    epd_dat(0x01);
    delay_ms(100);
    epd_all_idle();
}

static void ssd_refresh_finish(void) {
    if (!epd_wait_busy(1, HINK_BUSY_TIMEOUT_MS)) delay_ms(HINK_REFRESH_MS);
    ssd_deep_sleep();
}

/* ---- UC8151 -------------------------------------------------------- */
static void uc_init(void) {
    const panel_t *p = panel_active();
    epd_bs1(1);
    epd_reset_pulse(20);

    epd_cmd(0x06);              /* booster soft start */
    epd_dat(0x17);
    epd_dat(0x17);
    epd_dat(0x17);

    epd_cmd(0x04);              /* power on */
    if (!epd_wait_busy(0, 5000)) delay_ms(300);

    epd_cmd(0x00);              /* panel setting */
    epd_dat(0x8F);

    epd_cmd(0x50);              /* VCOM and data interval */
    epd_dat(0x77);

    epd_cmd(0x61);              /* TCON resolution: HRES, VRES */
    epd_dat((uint8_t)(p->width & 0xFF));
    epd_dat((uint8_t)(p->height & 0xFF));
    epd_dat((uint8_t)(p->height >> 8));

    epd_cmd(0x82);              /* VCM DC setting */
    epd_dat(0x0A);
}

static void uc_open_bw(void)  { epd_cmd(0x10); }

static void uc_open_red(void) {
    epd_cmd(0x92);              /* partial out: close the black frame */
    epd_cmd(0x13);
}

static void uc_refresh_start(void) {
    epd_cmd(0x92);
    epd_cmd(0x12);              /* display refresh */
}

static void uc_deep_sleep(void) {
    epd_cmd(0x02);              /* power off */
    delay_ms(100);
    epd_cmd(0x07);              /* deep sleep */
    epd_dat(0xA5);
    delay_ms(100);
    epd_all_idle();
}

static void uc_refresh_finish(void) {
    if (!epd_wait_busy(0, HINK_BUSY_TIMEOUT_MS)) delay_ms(HINK_REFRESH_MS);
    uc_deep_sleep();
}

const panel_t panel_hink_ssd1680 = {
    "HINK-E029A01", "SSD1680", 128, 296,
    2, 1, 1, HINK_REFRESH_MS,
    ssd_init, ssd_open_bw, ssd_open_red, ssd_refresh_start, ssd_refresh_finish,
    ssd_deep_sleep,
};

const panel_t panel_hink_uc8151 = {
    "HINK-E029A01", "UC8151", 128, 296,
    2, 0, 0, HINK_REFRESH_MS,
    uc_init, uc_open_bw, uc_open_red, uc_refresh_start, uc_refresh_finish,
    uc_deep_sleep,
};

/* 2.13" HINK-E0213A22-A0: 122 sources x 250 gates, so a RAM row is 16
 * bytes with the last 6 bits outside the glass. Tri-colour (B/W/R) on
 * the same SSD1680 command set as the 2.9" panel; assume the red plane
 * polarity measured on the 2.9" part (1 = red) until tested. */
const panel_t panel_hink_e0213a22 = {
    "HINK-E0213A22-A0", "SSD1680", 122, 250,
    2, 1, 1, 20000,
    ssd_init, ssd_open_bw, ssd_open_red, ssd_refresh_start, ssd_refresh_finish,
    ssd_deep_sleep,
};
