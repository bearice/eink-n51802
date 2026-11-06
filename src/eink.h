/* WFT0420CZ15 (Waveshare 4.2" e-Paper (B), 400x300 black/white/red)
 * driver for the nRF51802 tag board.
 *
 * The panel on this board is the UC8176 generation ("V1"); the full
 * command sequence was recovered from the stock firmware with Ghidra, see
 * eink.c for the details and the evidence.
 *
 * Transport is bit-banged 4-wire SPI, mode 0 (data changes while CLK is
 * low, sampled on the rising edge), MSB first.
 *
 * Rendering is procedural: pixels are computed on the fly while the two
 * bit-planes are streamed into the controller, so no frame buffer is
 * needed (one 400x300 1bpp plane alone is 15,000 bytes and this MCU only
 * has 16 KB RAM).
 */
#ifndef EINK_H
#define EINK_H

#include <stdint.h>

void eink_gpio_init(void);            /* P10..P16 */
void eink_reset(void);                /* hardware reset pulse sequence */
void eink_init(void);                 /* controller init for the variant */
void eink_clear(void);                /* all-white + full refresh */

/* Full refresh of the built-in boot information screen (header bar, info
 * lines, red footer). Returns when the panel has finished updating. */
void eink_show_test_pattern(void);

/* ---- streaming API for content delivered over BLE -------------------
 * Nothing is buffered: each byte goes straight into the panel's RAM, so
 * a full frame costs zero application RAM.
 *   eink_stream_begin()        reset + init + open the black plane (0x10)
 *   eink_stream_byte(b)        one image byte, MSB = leftmost pixel
 *   eink_stream_begin_red()    close black plane, open the red plane (0x13)
 *   eink_stream_end()          refresh (0x12), power off, deep sleep
 * The caller feeds EPD_BYTES bytes (400*300/8 = 15000) per plane. */
void eink_stream_begin(void);
void eink_partial_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  eink_partial_begin_checked(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void eink_set_keep_awake(int on);
int  eink_panel_ready(void);
void eink_stream_byte(uint8_t b);
void eink_stream_begin_red(void);
void eink_stream_end(void);
void eink_refresh_start(void);   /* send the refresh command, do not wait */
void eink_refresh_finish(void);  /* power off + deep sleep after the wait */
uint32_t eink_plane_bytes(void);

void eink_sleep(void);

/* Diagnostics: BUSY line sampling. BUSY is not wired to the stock
 * firmware's use (see eink.c), so these are evidence only.
 *   eink_busy_high_samples - 100 ms samples where BUSY was asserted
 *   eink_refresh_wait_ms   - how long the post-refresh wait took
 *   eink_last_status       - last 0x71 read-back (0 when unsupported) */
extern volatile uint32_t eink_busy_polls;
extern volatile uint32_t eink_busy_seen;
extern volatile uint32_t eink_busy_high_samples;
extern volatile uint32_t eink_refresh_wait_ms;
extern volatile uint32_t eink_last_status;
extern volatile uint32_t eink_busy_low_samples;
extern volatile uint32_t eink_busy_first_ms;
extern volatile uint32_t eink_busy_release_ms;

/* Controller generation for WFT0420CZ15 (Waveshare 4.2" B):
 *   EINK_V2 = UC8179 (new panel batch)
 *   EINK_V1 = UC8176 (old panel batch) - the one on this board
 * The init, plane commands and refresh command all differ; see eink.c. */
#define EINK_V2 0
#define EINK_V1 1
extern int eink_variant;
void eink_set_variant(int v);

#endif
