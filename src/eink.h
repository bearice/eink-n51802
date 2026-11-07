/* e-paper front end for the nRF51802 tag board.
 *
 * Several panels are supported and selected at run time (panel.h holds
 * the registry, `panel=<n>` in the configuration picks one). The active
 * panel's geometry drives everything here, so callers should ask for
 * eink_plane_bytes() rather than assume a resolution.
 *
 * Transport is bit-banged 4-wire SPI, mode 0, MSB first (see epd_bus.c).
 */
#ifndef EINK_H
#define EINK_H

#include <stdint.h>

#include "panel.h"

void eink_gpio_init(void);            /* P10..P16 */
void eink_init(void);                 /* reset + init the active panel */
void eink_clear(void);                /* all-white + full refresh */
void eink_show_test_pattern(void);    /* boot information screen */

/* ---- streaming API for content delivered over BLE -------------------
 * Nothing is buffered: each byte goes straight into the panel's RAM.
 *   eink_stream_begin()      reset + init, open the black plane
 *   eink_stream_byte(b)      one image byte, MSB = leftmost pixel
 *   eink_stream_begin_red()  close the black plane, open the red plane
 *   eink_stream_end()        refresh, power off, deep sleep
 * Feed eink_plane_bytes() bytes per plane.
 *
 * Logical encoding used by the API and by the BLE host tool:
 *   black plane  bit 1 = no black (white or red)
 *   red plane    bit 1 = red
 * The active panel descriptor says which wire encoding the red plane
 * actually needs (panel_t.red_one); eink_stream_byte() converts. */
void eink_stream_begin(void);
void eink_stream_byte(uint8_t b);
void eink_stream_begin_red(void);
void eink_stream_end(void);

/* Sending the refresh command and finishing the update are separate so
 * the BLE loop can keep servicing the SoftDevice during the ~15-20 s the
 * panel needs: refresh_start(), wait eink_refresh_ms(), refresh_finish(). */
void eink_refresh_start(void);
void eink_refresh_finish(void);      /* blocking: wait, then power down  */
void eink_refresh_complete(void);    /* power down now, no waiting       */
int  eink_panel_busy(void);          /* 1 while the panel is updating    */
void eink_sleep(void);

uint32_t eink_plane_bytes(void);      /* bytes per plane for this panel */
uint32_t eink_refresh_ms(void);       /* full-update time for this panel */
int      eink_panel_ready(void);
void     eink_set_keep_awake(int on);

void eink_partial_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  eink_partial_begin_checked(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Diagnostics for the UART log. BUSY is sampled, never trusted: when the
 * line is not connected the drivers fall back to fixed delays, and
 * eink_busy_used says which path ran.
 *   eink_busy_high_samples - samples where BUSY was asserted
 *   eink_refresh_wait_ms   - how long the last update took
 *   eink_last_status       - reserved (0): no usable status read-back */
extern volatile uint32_t eink_busy_polls;
extern volatile uint32_t eink_busy_seen;
extern volatile uint32_t eink_busy_high_samples;
extern volatile uint32_t eink_refresh_wait_ms;
extern volatile uint32_t eink_last_status;
extern volatile uint32_t eink_busy_low_samples;
extern volatile uint32_t eink_busy_first_ms;
extern volatile uint32_t eink_busy_release_ms;
extern volatile uint32_t eink_busy_used;

#endif
