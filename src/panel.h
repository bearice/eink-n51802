/* Panel registry: every supported e-paper panel + controller combination
 * as one descriptor, so the front end (eink.c) stays panel-agnostic and
 * a new panel is added by writing one more entry.
 *
 * Panels currently supported
 *   0  WFT0420CZ15 @ UC8176  4.2"  400x300 B/W/R  (original board)
 *   1  WFT0420CZ15 @ UC8179  4.2"  400x300 B/W/R  (newer panel batch)
 *   2  HINK-E029A01 @ SSD1680 2.9" 128x296 B/W/R
 *   3  HINK-E029A01 @ UC8151  2.9" 128x296 B/W/R
 *   4  HINK-E0213A22-A0 @ SSD1680 2.13" 122x250 B/W/R
 *
 * The HINK-E029A01 is sold under both controllers and their command sets
 * are not compatible, so both are compiled in and selected at run time
 * through the `panel=` configuration key (characteristic 0xFFF2).
 */
#ifndef PANEL_H
#define PANEL_H

#include <stdint.h>

typedef struct {
    const char *name;            /* panel part number                  */
    const char *ctrl;            /* controller IC                      */
    uint16_t width;              /* pixels across (X, source columns)  */
    uint16_t height;             /* pixels down   (Y, gate rows)       */
    uint8_t  planes;             /* 1 = B/W, 2 = B/W/R                 */
    uint8_t  red_one;            /* 1 = red plane bit 1 means RED,
                                  * 0 = bit 1 means NO red (inverted)  */
    uint8_t  busy_active_high;   /* BUSY level while the panel works   */
    uint32_t refresh_ms;         /* fixed fallback full-update time    */
    void (*init)(void);
    void (*open_bw)(void);
    void (*open_red)(void);
    void (*refresh_start)(void);
    void (*refresh_finish)(void);/* wait for the update, then power down */
    void (*deep_sleep)(void);    /* power down now, without waiting       */
} panel_t;

#define PANEL_WFT_UC8176    0
#define PANEL_WFT_UC8179    1
#define PANEL_HINK_SSD1680  2
#define PANEL_HINK_UC8151   3
#define PANEL_E0213A22_SSD1680 4
#define PANEL_COUNT         5

/* Panel assumed when the flash page holds no valid configuration. */
#ifndef CFG_PANEL_DEFAULT
#define CFG_PANEL_DEFAULT   PANEL_HINK_SSD1680
#endif

/* Bytes per gate row: the source count is not always a multiple of 8
 * (the 2.13" panel is 122 wide -> 16 bytes with 6 unused bits). */
static inline uint32_t panel_row_bytes(const panel_t *p) {
    return ((uint32_t)p->width + 7u) / 8u;
}

/* Bytes in one full plane. */
static inline uint32_t panel_plane_bytes(const panel_t *p) {
    return panel_row_bytes(p) * (uint32_t)p->height;
}

const panel_t *panel_get(int index);
const panel_t *panel_active(void);
int  panel_index(void);
void panel_select(int index);

/* ---- shared bit-banged SPI / GPIO (epd_bus.c) ---------------------- */
void epd_bus_init(void);                     /* P10..P16 as used by the panel */
void epd_cmd(uint8_t c);
void epd_dat(uint8_t d);
void epd_spi_byte(uint8_t b);
void epd_all_idle(void);                     /* every line high, BS1 idle     */
void epd_bs1(int active);                    /* 1 = talking to the panel      */
void epd_reset_pulse(uint32_t settle_ms);    /* single RST low pulse          */
int  epd_wait_busy(int busy_high, uint32_t timeout_ms);
int  epd_busy_asserted(int active_high);

/* BUSY diagnostics (epd_bus.c) */
extern volatile uint32_t epd_busy_samples;
extern volatile uint32_t epd_busy_first_ms;
extern volatile uint32_t epd_busy_release_ms;
extern volatile uint32_t epd_busy_used;

#endif
