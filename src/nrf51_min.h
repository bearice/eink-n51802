/* Minimal nRF51 (nRF51802) register definitions.
 * Only the peripherals this firmware uses: GPIO, UART0, CLOCK/POWER,
 * TIMER0/1/2 and RTC1. All offsets verified against the official Nordic
 * CMSIS header nrf51.h (SVDConv-generated from nrf51.svd):
 *   https://raw.githubusercontent.com/sandeepmistry/arduino-nRF5/master/cores/nRF5/SDK/components/device/nrf51.h
 * CROSS-CHECKED against the board's stock firmware (nrf51802-flash-256k.bin)
 * decompiled with Ghidra: it reads/writes 0x50000500+0x8/0xC (GPIO OUTSET/
 * OUTCLR), 0x50000700+n*4 (PIN_CNF), 0x40000000 (CLOCK TASKS_HFCLKSTART) and
 * 0x4000040C (HFCLKSTAT) - all consistent with this layout.
 *
 * NOTE: an early version of this header used nRF52 offsets (HFCLKSTAT 0x424,
 * POWER base 0x4001E000, PIN_CNFG 0x560+, TASKS_STARTUART/STOPUART). Those
 * do not exist on nRF51; polling 0x40000424 hung forever, which is why the
 * first flashed firmware appeared to do nothing.
 */
#ifndef NRF51_MIN_H
#define NRF51_MIN_H

#include <stdint.h>

#define REG(base, off)  (*(volatile uint32_t *)((base) + (off)))

/* ---- GPIO (P0), base 0x50000000 ---- */
#define GPIO_BASE       0x50000000UL
#define GPIO_OUT        0x504UL
#define GPIO_OUTSET     0x508UL
#define GPIO_OUTCLR     0x50CUL
#define GPIO_IN         0x510UL
#define GPIO_DIR        0x514UL
#define GPIO_DIRSET     0x518UL
#define GPIO_DIRCLR     0x51CUL
#define GPIO_PIN_CNFG(n) (0x700UL + ((uint32_t)(n) * 4UL))

/* PIN_CNFG (nRF51: DIR bit0, INPUT bit1, PULL bits2-3, DRIVE bits4-5).
 * Verified on hardware: bit1 = 0 CONNECTS the input buffer, bit1 = 1
 * disconnects it, and a disconnected pin always reads 0. Measured on P9:
 *   PIN_CNF = 0x0C (pull-up)          -> reads 1
 *   PIN_CNF = 0x0E (pull-up + bit1)   -> reads 0
 * The stock firmware agrees: it writes PIN_CNF[9] = 0x00020000, i.e.
 * input connected, no pull, SENSE = high. */
#define CFG_OUTPUT        0x01UL           /* S0D0 push-pull output           */
#define CFG_INPUT_NOPULL  0x00UL           /* input connected, no pull        */
#define CFG_INPUT_PULLUP  0x0CUL           /* input connected, pull-up 3<<2   */
#define CFG_INPUT_PULLDOWN 0x04UL          /* input connected, pull-down 1<<2 */
/* nRF51 PIN_CNF SENSE field is bits 16-17: 2 = wake on high level, 3 = low.
 * The connected(input-buffer) state is bit1 = 0, so this is also input. */
#define CFG_INPUT_SENSE_HIGH 0x00020000UL  /* input, no pull, sense=high      */
#define CFG_INPUT_SENSE_LOW  0x00030000UL  /* input, no pull, sense=low       */
#define CFG_INPUT_SENSE_HIGH_PD 0x00020004UL /* input, pull-DOWN, sense=high  */

/* ---- CLOCK/POWER (shared block, base 0x40000000) ---- */
#define CLOCK_BASE      0x40000000UL
#define POWER_BASE      0x40000000UL
#define CLOCK_TASKS_STARTHFCLK  0x000UL
#define CLOCK_TASKS_STOPHFCLK   0x004UL
#define CLOCK_TASKS_STARTLFCLK  0x008UL
#define CLOCK_EVENTS_HFCLKSTARTED 0x100UL
#define CLOCK_EVENTS_LFCLKSTARTED 0x104UL
#define CLOCK_EVENTS_DONE 0x10CUL
#define CLOCK_HFCLKRUN  0x408UL
#define CLOCK_HFCLKSTAT 0x40CUL
#define CLOCK_LFCLKSTAT 0x418UL
#define CLOCK_LFCLKSRC  0x518UL
#define POWER_TASKS_CONSTLAT 0x078UL       /* keep HFCLK on (const latency)   */
#define POWER_RESETREAS 0x400UL            /* read & clear reset reason       */
#define POWER_RAMON     0x524UL

/* ---- UART0, base 0x40002000 ---- */
#define UART0_BASE      0x40002000UL
#define UART0_TASKS_STARTRX  0x000UL
#define UART0_TASKS_STOPRX   0x004UL
#define UART0_TASKS_STARTTX  0x008UL
#define UART0_TASKS_STOPTX   0x00CUL
#define UART0_TASKS_SUSPEND  0x01CUL
#define UART0_EVENTS_RXDRDY  0x108UL
#define UART0_EVENTS_TXDRDY  0x11CUL
#define UART0_EVENTS_ERROR   0x124UL
#define UART0_ENABLE      0x500UL
#define UART0_PSEL_RTS    0x508UL
#define UART0_PSEL_TXD    0x50CUL
#define UART0_PSEL_CTS    0x510UL
#define UART0_PSEL_RXD    0x514UL
#define UART0_RXD         0x518UL
#define UART0_TXD         0x51CUL
#define UART0_BAUDRATE    0x524UL
#define UART0_CONFIG      0x56CUL

/* nRF51 ENABLE field is bits [2:0] and "enabled" is 4, not 1
 * (nrf51_bitfields.h: UART_ENABLE_ENABLE_Enabled = 0x04). Writing 1 leaves
 * the UART disabled: no TXD drive, no EVENTS_TXDRDY. That bug is why this
 * project wrongly concluded for a while that the hardware UART was broken. */
#define UART_ENABLE_ENABLED 4UL

/* nRF51 baud table (nrf51_bitfields.h) - NOT the nRF52 encoding:
 * nRF52's 57600 = 0x00B00000 does not exist on nRF51. */
#define UART_BAUD_57600   0x00EBF000UL
#define PIN_DISCONNECTED  0xFFUL

/* ---- Board pin map (P0 pins) ----
 * Recovered from the stock firmware with Ghidra (see eink.c): the e-ink
 * pins are exactly P10..P14/P16 with P15 unused, matching the board's
 * "SoC IO in order to the panel FPC" wiring. */
#define PIN_LED1   3    /* low-active LED 1 (power/boot)                 */
#define PIN_LED2   4    /* low-active LED 2 (BLE state)                  */
#define PIN_LED3   5    /* low-active LED 3 (e-ink busy)                 */
#define PIN_UART_TX 8   /* to RP2040 debugprobe UART RX (57600 8N1)      */
#define PIN_WAKE 9      /* RF/NFC coil coupling input, external 150k pulldown */
#define PIN_EPD_IO 10   /* shared data line: DIN out + status read back  */
#define PIN_EPD_CK 11   /* SPI clock                                     */
#define PIN_EPD_CS 12   /* chip select, active low                       */
#define PIN_EPD_DC 13   /* low = command, high = data                    */
#define PIN_EPD_RST 14  /* reset, active low                             */
#define PIN_EPD_BUSY 15 /* NOT used by the stock firmware; unreadable    */
#define PIN_EPD_BS1 16  /* driven LOW during panel activity, HIGH idle   */

/* ---- GPIOTE, base 0x40006000 ----
 * Used only for the P9 PORT event: with PIN_CNF.SENSE set, the DETECT
 * signal raises EVENTS_PORT, which is the only way to notice an RF-coil
 * pulse while the MCU is running (INTENSET bit 31 = PORT). */
#define GPIOTE_BASE        0x40006000UL
#define GPIOTE_TASKS_OUT(n)   (0x000UL + (uint32_t)(n) * 4UL)
#define GPIOTE_EVENTS_IN(n)   (0x100UL + (uint32_t)(n) * 4UL)
#define GPIOTE_EVENTS_PORT    0x17CUL
#define GPIOTE_INTENSET       0x304UL
#define GPIOTE_INTENCLR       0x308UL
#define GPIOTE_CONFIG(n)      (0x510UL + (uint32_t)(n) * 4UL)
#define GPIOTE_POWER          0xFFCUL
#define GPIOTE_PSEL_PORT      31UL          /* CONFIG.PSEL = 31 -> port event */
#define GPIOTE_MODE_EVENT     1UL
#define GPIOTE_IRQN           6

/* ---- tiny helpers ---- */
static inline void gpio_dir_output(uint32_t pin) {
    REG(GPIO_BASE, GPIO_PIN_CNFG(pin)) = CFG_OUTPUT;
    REG(GPIO_BASE, GPIO_DIRSET) = (1UL << pin);
}
static inline void gpio_dir_input(uint32_t pin, uint32_t cfg) {
    REG(GPIO_BASE, GPIO_PIN_CNFG(pin)) = cfg;
    REG(GPIO_BASE, GPIO_DIRCLR) = (1UL << pin);
}
static inline void gpio_write(uint32_t pin, uint32_t level) {
    if (level) REG(GPIO_BASE, GPIO_OUTSET) = (1UL << pin);
    else       REG(GPIO_BASE, GPIO_OUTCLR) = (1UL << pin);
}
static inline uint32_t gpio_read(uint32_t pin) {
    return (REG(GPIO_BASE, GPIO_IN) >> pin) & 1UL;
}

/* ---- TIMER0/1/2, base 0x40008000 + n*0x1000 ----
 * nRF51 semantics that cost a full day of debugging:
 *   MODE = 0 -> Timer   (counts PCLK16M)
 *   MODE = 1 -> Counter (counts pin edges; CC[0] then never advances)
 *   BITMODE: 0=16-bit, 1=8-bit, 2=24-bit, 3=32-bit  (nRF52 order differs)
 *   Reading CC[n] returns the stored capture/compare value, NEVER the live
 *   counter. The live value is only visible through TASKS_CAPTURE[n] or an
 *   EVENTS_COMPARE[n] match.
 * Measured on this board with TASKS_CAPTURE: 32,012,005 ticks over a 2.000 s
 * sleep => TIMER0 counts at 16.006 MHz. */
#define TIMER0_BASE        0x40008000UL
#define TIMER1_BASE        0x40009000UL
#define TIMER2_BASE        0x4000A000UL
#define TIMER0_TASKS_START 0x000UL
#define TIMER0_TASKS_STOP  0x004UL
#define TIMER0_TASKS_COUNT 0x008UL
#define TIMER0_TASKS_CLEAR 0x00CUL
#define TIMER0_TASKS_SHUTDOWN 0x010UL
#define TIMER0_TASKS_CAPTURE(n)  (0x040UL + (uint32_t)(n) * 4UL)
#define TIMER0_EVENTS_COMPARE(n) (0x140UL + (uint32_t)(n) * 4UL)
#define TIMER0_SHORTS      0x200UL
#define TIMER0_INTENSET    0x304UL
#define TIMER0_INTENCLR    0x308UL
#define TIMER0_MODE        0x504UL
#define TIMER0_BITMODE     0x508UL
#define TIMER0_PRESCALER   0x510UL
#define TIMER0_CC(n)       (0x540UL + (uint32_t)(n) * 4UL)
#define TIMER0_POWER       0xFFCUL

#define TIMER_MODE_TIMER   0UL
#define TIMER_MODE_COUNTER 1UL
#define TIMER_BITMODE_8    1UL
#define TIMER_BITMODE_16   0UL
#define TIMER_BITMODE_24   2UL
#define TIMER_BITMODE_32   3UL
#define TIMER_SHORT_COMPARE0_CLEAR 1UL

#define TICKS_PER_MS 16000UL
#define TICKS_PER_US 16UL

/* Which TIMER instance is the app time base. S110 reserves TIMER0/RTC0, so
 * BLE images build with -DAPP_TIMER=1. Note: with S110 resident, TIMER1/2
 * measured ~60 kHz and 16-bit-only over SWD, so they are NOT used for
 * delays any more - RTC1 is (see below). */
#ifndef APP_TIMER
#define APP_TIMER 0
#endif
#if   APP_TIMER == 0
#define TIMER_APP_BASE TIMER0_BASE
#elif APP_TIMER == 1
#define TIMER_APP_BASE TIMER1_BASE
#elif APP_TIMER == 2
#define TIMER_APP_BASE TIMER2_BASE
#else
#error "APP_TIMER must be 0, 1 or 2"
#endif

static inline void app_timer_init(void) {
    REG(TIMER_APP_BASE, TIMER0_POWER)       = 1UL;
    REG(TIMER_APP_BASE, TIMER0_MODE)        = TIMER_MODE_TIMER;
    REG(TIMER_APP_BASE, TIMER0_BITMODE)     = TIMER_BITMODE_32;
    REG(TIMER_APP_BASE, TIMER0_PRESCALER)   = 0UL;   /* 16 MHz */
    REG(TIMER_APP_BASE, TIMER0_TASKS_CLEAR) = 1UL;
    REG(TIMER_APP_BASE, TIMER0_TASKS_START) = 1UL;
}

/* Relative wait of `ticks` 16 MHz ticks. */
static inline void app_timer_wait_ticks(uint32_t ticks) {
    REG(TIMER_APP_BASE, TIMER0_TASKS_CLEAR) = 1UL;
    REG(TIMER_APP_BASE, TIMER0_CC(0)) = ticks;
    REG(TIMER_APP_BASE, TIMER0_EVENTS_COMPARE(0)) = 0UL;
    while (REG(TIMER_APP_BASE, TIMER0_EVENTS_COMPARE(0)) == 0UL) {
        __asm__ volatile ("nop");
    }
}

/* ---- RTC1, base 0x40011000 -----------------------------------------
 * 24-bit counter clocked from LFCLK (32768 Hz, prescaler 0 => 30.5 us per
 * tick). This is the application time base: S110 owns RTC0 and TIMER0, and
 * TIMER1/TIMER2 misbehave under the SoftDevice (see above). */
#define RTC1_BASE          0x40011000UL
#define RTC_TASKS_START    0x000UL
#define RTC_TASKS_STOP     0x004UL
#define RTC_TASKS_CLEAR    0x008UL
#define RTC_EVENTS_COMPARE(n) (0x140UL + (uint32_t)(n) * 4UL)
#define RTC_INTENSET       0x304UL
#define RTC_COUNTER        0x504UL
#define RTC_PRESCALER      0x508UL
#define RTC_CC(n)          (0x540UL + (uint32_t)(n) * 4UL)
#define RTC_POWER          0xFFCUL
#define RTC_COUNTER_MASK   0x00FFFFFFUL

/* Start the LFCLK from the internal RC oscillator. The SoftDevice uses the
 * same source later (RC with 1000 ms calibration). */
static inline uint32_t lfclk_start_rc(void) {
    uint32_t t = 0;
    REG(CLOCK_BASE, CLOCK_LFCLKSRC) = 0UL;             /* 0 = RC */
    REG(CLOCK_BASE, CLOCK_TASKS_STARTLFCLK) = 1UL;
    while (!(REG(CLOCK_BASE, CLOCK_LFCLKSTAT) & 1UL)) {
        if (++t > 200000UL) return 1UL;
    }
    return 0UL;
}

static inline void rtc_init(void) {
    REG(RTC1_BASE, RTC_POWER)       = 1UL;
    REG(RTC1_BASE, RTC_PRESCALER)   = 0UL;             /* 32768 Hz ticks */
    REG(RTC1_BASE, RTC_TASKS_CLEAR) = 1UL;
    REG(RTC1_BASE, RTC_TASKS_START) = 1UL;
}

/* Milliseconds since RTC1 start; wraps every 512 s. Compare deadlines with
 * modular subtraction. One LFCLK tick is 30.5 us, so ms = c*125/4096 and
 * the 24-bit counter keeps the product inside 32 bits. */
static inline uint32_t rtc_ms(void) {
    uint32_t c = REG(RTC1_BASE, RTC_COUNTER) & RTC_COUNTER_MASK;
    return (c * 125UL) >> 12;
}

/* delay_ms uses RTC1 (32.768 ticks per ms, so the call is exact); delay_us
 * is a short calibrated nop loop (the e-ink SPI no longer needs it).
 * The old pure nop-loop delay_ms was ~4x off: delay_ms(250) took 3.9 s. */
static inline void delay_ms(uint32_t ms) {
    if (!ms) return;
    uint32_t start = REG(RTC1_BASE, RTC_COUNTER) & RTC_COUNTER_MASK;
    uint32_t want = (ms * 8192UL) / 250UL;             /* ms * 32.768 */
    while ((((REG(RTC1_BASE, RTC_COUNTER) & RTC_COUNTER_MASK) - start)
            & RTC_COUNTER_MASK) < want) { }
}

static inline void delay_us(uint32_t us) {
    volatile uint32_t n = us * 4UL;                    /* ~4 cycles each */
    while (n--) { __asm__ volatile ("nop"); }
}

/* Start HFCLK (16 MHz crystal; nRF51 has NO PLL - the core itself runs at
 * 16 MHz) and wait until it runs. Returns 0 on success, 1 on timeout. */
static inline uint32_t clock_start_hfclk_timeout(void) {
    uint32_t t = 0;
    REG(CLOCK_BASE, CLOCK_TASKS_STARTHFCLK) = 1UL;
    while (!(REG(CLOCK_BASE, CLOCK_HFCLKSTAT) & 1UL)) {
        if (++t > 6400000UL) return 1UL;
    }
    return 0UL;
}

static inline void clock_start_hfclk(void) {
    (void)clock_start_hfclk_timeout();
}

#endif
/* nrf51_min.h */
