/* UC8179 (WFT0420CZ15 3-colour / GDEW042T2, Waveshare 4.2in 400x300
 * black/white/red) driver.
 *
 * Init table is verbatim from Waveshare's official epd4in2b_V2 driver
 * (Init_new()), which is known-good for this panel/controller pair.
 * The controller's red RAM (0x26) takes inverted image data, exactly as
 * in Waveshare Display_new().
 *
 * BUSY polarity: the new (UC8179) controller drives BUSY HIGH while busy
 * (Waveshare ReadBusy() waits while BUSY==1 for the new controller).
 * Every wait also has an 8 s timeout so a wrong assumption can never
 * wedge the chip.
 */
#include "eink.h"
#include "nrf51_min.h"

/* BUSY is NOT usable on this board: the original (working) firmware runs
 * with GPIO DIR = 0x00017C38, i.e. P10,P11,P12,P13,P14,P16 as outputs and
 * P15 left completely unconfigured (PIN_CNF[15] = 0, input buffer off).
 * So the stock firmware never reads BUSY and instead waits with fixed
 * delays. We do the same; BUSY is only *sampled* as evidence that the
 * panel actually started a refresh (see refresh_and_measure()).
 *
 * Pin order comes from the board wiring (SoC IO in order to the panel
 * FPC): P10=DIN, P11=CK, P12=CS, P13=DC, P14=RST, P15=BUSY(unused),
 * P16=BS1. The stock firmware drives P16 HIGH, so BS1 = 1 here.
 */
#define EPD_BUSY_ACTIVE_HIGH 1
#define EPD_BS1_LEVEL 1
#define EPD_WIDTH   400
#define EPD_HEIGHT  300
#define EPD_ROWBYTES (EPD_WIDTH / 8)   /* 50 bytes per row */

static int s_panel_ready;           /* panel initialised, RAM valid      */
static int s_keep_awake = 1;         /* 1 = never deep-sleep the panel    */

void eink_set_keep_awake(int on) { s_keep_awake = on; }
int eink_panel_ready(void) { return s_panel_ready; }

static void spi_bit(uint32_t bit) {
    /* Mode 0, MSB first: drive the data line while CLK is low, then raise
     * CLK. This is exactly the stock firmware's bit loop (FUN_00000ac4):
     * P11 low, set P10, P11 high - with no inter-edge delays. */
    gpio_write(PIN_EPD_CK, 0);
    gpio_write(PIN_EPD_IO, bit);
    gpio_write(PIN_EPD_CK, 1);
}

static void spi_byte(uint8_t b) {
    for (int i = 7; i >= 0; i--) spi_bit((b >> i) & 1);
}

static void cmd(uint8_t c) {
    gpio_write(PIN_EPD_DC, 0);
    gpio_write(PIN_EPD_CS, 0);
    spi_byte(c);
    gpio_write(PIN_EPD_CS, 1);
}

static void dat(uint8_t d) {
    gpio_write(PIN_EPD_DC, 1);
    gpio_write(PIN_EPD_CS, 0);
    spi_byte(d);
    gpio_write(PIN_EPD_CS, 1);
}

volatile uint32_t eink_busy_polls;      /* diagnostics: last wait length  */
volatile uint32_t eink_busy_seen;       /* diagnostics: BUSY was high     */
/* Sample BUSY while the panel works. Returns the number of 100 ms samples
 * where BUSY was asserted, and how long we waited. If the panel really
 * started a refresh and BUSY is wired, this is > 0; if it stays 0 the
 * panel never began (or BUSY is not connected, which the stock firmware
 * suggests).
 */
volatile uint32_t eink_busy_high_samples;
volatile uint32_t eink_refresh_wait_ms;

static uint32_t refresh_and_measure(uint32_t max_ms) {
    uint32_t high = 0, seen_high_at = 0, ms = 0;
    for (; ms < max_ms; ms += 100) {
        if (gpio_read(PIN_EPD_BUSY)) {
            high++;
            seen_high_at = ms;
        } else if (high && (ms - seen_high_at) >= 300) {
            ms += 100;                  /* BUSY released: refresh done */
            break;
        }
        delay_ms(100);
    }
    eink_busy_high_samples = high;
    eink_refresh_wait_ms = ms;
    return ms;
}

static void wait_idle(void) {
    /* BUSY is unread by the stock firmware and reads 0 here, so this only
     * returns immediately. Kept so the code stays correct if a future
     * board revision does wire BUSY. */
    uint32_t guard = 0;
    eink_busy_polls = 0;
    eink_busy_seen = 0;
    while (guard++ < 32000000UL) {
#if EPD_BUSY_ACTIVE_HIGH
        if (gpio_read(PIN_EPD_BUSY)) eink_busy_seen = 1;
        if (gpio_read(PIN_EPD_BUSY) == 0) { eink_busy_polls = guard; return; }
#else
        if (gpio_read(PIN_EPD_BUSY) == 0) eink_busy_seen = 1;
        if (gpio_read(PIN_EPD_BUSY) == 1) { eink_busy_polls = guard; return; }
#endif
    }
    eink_busy_polls = 0xFFFFFFFFUL;     /* timed out */
}

void eink_gpio_init(void) {
    gpio_dir_output(PIN_EPD_IO);
    gpio_dir_output(PIN_EPD_CK);
    gpio_dir_output(PIN_EPD_CS);
    gpio_dir_output(PIN_EPD_DC);
    gpio_dir_output(PIN_EPD_RST);
    gpio_dir_output(PIN_EPD_BS1);
    gpio_write(PIN_EPD_CK, 0);     /* idle low (mode 0)  */
    gpio_write(PIN_EPD_CS, 1);     /* deasserted         */
    gpio_write(PIN_EPD_DC, 0);
    gpio_write(PIN_EPD_RST, 1);
    gpio_write(PIN_EPD_IO, 0);
    gpio_write(PIN_EPD_BS1, EPD_BS1_LEVEL);  /* stock fw drives this HIGH */
    gpio_dir_input(PIN_EPD_BUSY, CFG_INPUT_NOPULL);
}

/* ---- controller generations ---------------------------------------
 * The stock firmware in nrf51802-flash-256k.bin is a UC8176 ("old") driver
 * beyond doubt. Recovered with Ghidra (ghidra_dump.c) it does:
 *   0x04 power on -> 0x71 busy poll -> 0x00 {0x0F,0x0D}
 *   -> 0x10 (black plane) -> 0x13 (red plane) -> 0x12 (refresh)
 *   -> 0x50=0xF7 -> 0x02 power off -> 0x07=0xA5 deep sleep
 * That command set only exists on UC8176 (the UC8179/UC8179 "V2" uses
 * 0x12 swreset, 0x3C, 0x18, 0x11, 0x44/0x45, 0x22=0xF7 + 0x20 instead,
 * and has no 0x02/0x07 sleep pair). Pin roles also come from the same
 * decompilation: P10 = shared data (DIN + read-back), P11 = CLK,
 * P12 = CS, P13 = DC, P14 = RST, P16 = BS1/power strap held LOW during
 * all activity and HIGH at idle, P15 unused.
 *
 * Its delays are fixed, not BUSY driven: FUN_00000340(n) burns n*999 nop
 * iterations, and its 0x71 read-back samples P10 with the input buffer
 * disconnected (so it always reads 0 and the 2000-poll loop just times
 * out). We mimic that with explicit delays and only poll BUSY as an
 * optional bonus (input buffer enabled, short timeout).
 */
int eink_variant = EINK_V1;

void eink_set_variant(int v) { eink_variant = v; }

static void epd_all_idle(void) {
    /* stock FUN_00000b38: every control line high at idle */
    gpio_write(PIN_EPD_IO, 1);
    gpio_write(PIN_EPD_CK, 1);
    gpio_write(PIN_EPD_CS, 1);
    gpio_write(PIN_EPD_DC, 1);
    gpio_write(PIN_EPD_BS1, 1);
    gpio_write(PIN_EPD_RST, 1);
}

/* Simple reset used by the UC8179 path (the UC8176 path uses the stock
 * three-pulse reset inside init_v1). */
void eink_reset(void) {
    gpio_write(PIN_EPD_RST, 1);
    delay_ms(20);
    gpio_write(PIN_EPD_RST, 0);
    delay_ms(2);
    gpio_write(PIN_EPD_RST, 1);
    delay_ms(20);
    gpio_write(PIN_EPD_CS, 1);
}

/* Read one byte back from the panel (the 0x71 status command does this).
 * Stock does the same: DC high, CS low, clock 8 bits in and sample the
 * shared data line P10 - but it leaves PIN_CNF[10] = 0 (input buffer
 * disconnected), so it always reads 0 and its 2000-poll loop simply times
 * out. We enable the buffer so a live panel can actually answer. */
static uint32_t epd_read_byte(void) {
    uint32_t v = 0;
    gpio_dir_input(PIN_EPD_IO, CFG_INPUT_NOPULL);
    gpio_write(PIN_EPD_CS, 0);
    gpio_write(PIN_EPD_DC, 1);
    for (int i = 0; i < 8; i++) {
        gpio_write(PIN_EPD_CK, 0);
        v = (v << 1) | gpio_read(PIN_EPD_IO);
        gpio_write(PIN_EPD_CK, 1);
    }
    gpio_write(PIN_EPD_CS, 1);
    gpio_dir_output(PIN_EPD_IO);
    gpio_write(PIN_EPD_IO, 0);
    return v;
}

/* Poll 0x71 until bit0 (busy flag) is 1, or until timeout. Mirrors stock
 * FUN_00000bcc (up to 2000 polls, 20 delay units apart). */
volatile uint32_t eink_last_status;     /* most recent 0x71 read-back */

static void epd_wait_busy(uint32_t timeout_ms) {
    uint32_t waited = 0;
    for (;;) {
        cmd(0x71);
        eink_last_status = epd_read_byte();
        if (eink_last_status & 1UL) return;
        if (waited >= timeout_ms) return;
        delay_ms(20);
        waited += 20;
    }
}

static void init_v2(void) {
    cmd(0x12);            /* SWRESET */
    delay_ms(10);
    cmd(0x3C);            /* Border waveform */
    dat(0x05);
    delay_ms(2);
    cmd(0x18);            /* internal temperature sensor */
    dat(0x80);
    delay_ms(2);
    cmd(0x11);            /* data entry mode */
    dat(0x03);
    delay_ms(2);
    cmd(0x44);            /* X address start/end (400/8-1 = 0x31) */
    dat(0x00);
    dat(0x31);
    cmd(0x45);            /* Y address start/end (299 = 0x012B) */
    dat(0x00);
    dat(0x00);
    dat(0x2B);
    dat(0x01);
    cmd(0x4E);            /* X counter = 0 */
    dat(0x00);
    cmd(0x4F);            /* Y counter = 0 */
    dat(0x00);
    dat(0x00);
    delay_ms(2);
    s_panel_ready = 1;
}

/* Stock reset (FUN_00000c08): BS1 low, then three RST low/high pulses of
 * ~1.5 ms each, then a ~175 ms settle. */
static void reset_v1(void) {
    gpio_write(PIN_EPD_BS1, 0);
    for (int i = 0; i < 3; i++) {
        gpio_write(PIN_EPD_RST, 0);
        delay_ms(2);
        gpio_write(PIN_EPD_RST, 1);
        delay_ms(2);
    }
    delay_ms(180);
}

static uint32_t epd_wait_ready(uint32_t timeout_ms);   /* below */

static void init_v1(void) {
    reset_v1();
    cmd(0x04);            /* power on */
    epd_wait_ready(5000);    /* power-on ramp is ~1 s */
    cmd(0x00);            /* panel setting */
    dat(0x0F);
    dat(0x0D);
    s_panel_ready = 1;    /* RAM is now defined for windowed writes */
}

void eink_init(void) {
    if (eink_variant == EINK_V2) init_v2();
    else                         init_v1();
}

/* Wait until the panel reports ready via the 0x71 status byte.
 * Measured on this board: after 0x12 the busy flag is low from the first
 * poll and clears after ~14.2 s, so this replaces the fixed 20 s wait and
 * saves ~6 s per refresh. */
volatile uint32_t eink_busy_ms;

static uint32_t epd_wait_ready(uint32_t timeout_ms) {
    uint32_t t0 = rtc_ms();
    uint32_t polls = 0;
    for (;;) {
        cmd(0x71);
        uint32_t st = epd_read_byte();
        eink_last_status = st;
        polls++;
        if (st & 1UL) break;                       /* bit0 = ready */
        if ((rtc_ms() - t0) >= timeout_ms) break;
        delay_ms(10);
    }
    eink_busy_polls = polls;
    eink_busy_ms = rtc_ms() - t0;
    return eink_busy_ms;
}

/* Probe the 0x71 status while the panel refreshes: does bit0 (busy flag)
 * actually go low while it works? The stock firmware polls it but leaves
 * PIN_CNF[10]=0 (input buffer disconnected) so it always read 0 and fell
 * back to a fixed timeout. We now read it for real, so measure before
 * trusting it. */
volatile uint32_t eink_busy_low_samples;   /* polls that reported "busy"  */
volatile uint32_t eink_busy_first_ms;      /* first busy report           */
volatile uint32_t eink_busy_release_ms;    /* when it reported ready again*/

static uint32_t probe_busy(uint32_t duration_ms) {
    uint32_t t0 = rtc_ms();
    uint32_t low = 0, first = 0xFFFFFFFFUL, rel = 0xFFFFFFFFUL, last = 0;
    while ((rtc_ms() - t0) < duration_ms) {
        cmd(0x71);
        last = epd_read_byte();
        eink_last_status = last;
        if ((last & 1UL) == 0) {
            low++;
            if (first == 0xFFFFFFFFUL) first = rtc_ms() - t0;
        } else if (low && rel == 0xFFFFFFFFUL) {
            rel = rtc_ms() - t0;
        }
        delay_ms(100);
    }
    eink_busy_low_samples = low;
    eink_busy_first_ms = first;
    eink_busy_release_ms = rel;
    eink_refresh_wait_ms = rtc_ms() - t0;
    return rel;
}

static void turn_on_display(void) {
    if (eink_variant == EINK_V2) {
        cmd(0x22);        /* Display Update Control 2 */
        dat(0xF7);        /* full refresh */
        cmd(0x20);        /* activate */
        refresh_and_measure(25000);
        return;
    }
    /* UC8176: refresh, then the stock shutdown tail. */
    cmd(0x12);            /* display refresh */
    epd_wait_ready(25000);   /* real busy handshake (~14.2 s here) */
    cmd(0x50);            /* VCOM and data interval */
    dat(0xF7);
    delay_ms(100);
    cmd(0x02);            /* power off */
    epd_wait_ready(5000);
    delay_ms(200);
    cmd(0x07);            /* deep sleep */
    dat(0xA5);
    delay_ms(300);
    epd_all_idle();
}

void eink_clear(void) {
    if (eink_variant == EINK_V2) {
        cmd(0x24);
        for (uint32_t i = 0; i < (uint32_t)EPD_ROWBYTES * EPD_HEIGHT; i++) dat(0xFF);
        cmd(0x26);
        for (uint32_t i = 0; i < (uint32_t)EPD_ROWBYTES * EPD_HEIGHT; i++) dat(0x00);
    } else {
        cmd(0x10);
        for (uint32_t i = 0; i < (uint32_t)EPD_ROWBYTES * EPD_HEIGHT; i++) dat(0xFF);
        cmd(0x13);
        for (uint32_t i = 0; i < (uint32_t)EPD_ROWBYTES * EPD_HEIGHT; i++) dat(0xFF);
    }
    turn_on_display();
}
/* ---- boot information screen ---------------------------------------
 * Rendered procedurally straight into the panel's two bit-planes, so it
 * needs no frame buffer (this MCU only has 16 KB RAM, and one 400x300
 * 1bpp plane is already 15,000 bytes). A 5x7 font is included below.
 * Colour model: black plane bit 0 = black; red plane bit 0 = red
 * (red plane is sent inverted, 0xFF = no red). */
enum { COL_WHITE = 0, COL_BLACK = 1, COL_RED = 2 };

/* 5x7 glyphs, column-major, bit0 = top row. Classic terminal font. */
static const uint8_t *glyph(char c) {
    static const uint8_t sp[5] = {0, 0, 0, 0, 0};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t dot[5]  = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t zero[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t one[5]  = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t two[5]  = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t three[5]= {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t four[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t five[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t six[5]  = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t seven[5]= {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t eight[5]= {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t nine[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
    static const uint8_t A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    static const uint8_t E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t G[5] = {0x3E, 0x41, 0x49, 0x49, 0x72};
    static const uint8_t H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t W[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
    static const uint8_t X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
    static const uint8_t Y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
    static const uint8_t Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

    switch (c) {
    case ' ': return sp;
    case '-': return dash;
    case '.': return dot;
    case '0': return zero;  case '1': return one;  case '2': return two;
    case '3': return three; case '4': return four; case '5': return five;
    case '6': return six;   case '7': return seven;case '8': return eight;
    case '9': return nine;
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
    case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
    case 'I': return I; case 'K': return K; case 'L': return L; case 'M': return M;
    case 'N': return N; case 'O': return O; case 'P': return P; case 'R': return R;
    case 'S': return S; case 'T': return T; case 'U': return U; case 'V': return V;
    case 'W': return W; case 'X': return X; case 'Y': return Y; case 'Z': return Z;
    default:  return sp;
    }
}

/* Is pixel (x,y) inside the text `s` drawn at (tx,ty) with `scale`? */
static int text_hit(const char *s, int tx, int ty, int scale, uint32_t x, uint32_t y) {
    int cell = 6 * scale;
    int gx = (int)x - tx;
    int gy = (int)y - ty;
    if (gx < 0 || gy < 0 || gy >= 7 * scale) return 0;
    int idx = gx / cell;
    int cx = gx - idx * cell;
    if (cx >= 5 * scale) return 0;              /* inter-character gap */
    const char *p = s;
    for (int i = 0; i < idx; i++) {
        if (!*p) return 0;
        p++;
    }
    if (!*p) return 0;
    const uint8_t *g = glyph(*p);
    return (g[cx / scale] >> (gy / scale)) & 1;
}

/* Horizontal start for centred text. */
static int centre_x(const char *s, int scale) {
    int n = 0;
    while (s[n]) n++;
    return (EPD_WIDTH - n * 6 * scale) / 2;
}

/* The screen: header bar, info lines, red footer bar. */
static int screen_colour(uint32_t x, uint32_t y) {
    const int frame = 3;

    /* 3 px black frame around the whole panel */
    if (x < (uint32_t)frame || x >= (uint32_t)(EPD_WIDTH - frame) ||
        y < (uint32_t)frame || y >= (uint32_t)(EPD_HEIGHT - frame)) {
        return COL_BLACK;
    }

    /* Header bar: white title on black */
    if (y < 58) {
        if (text_hit("EINK-N51802", 56, 20, 3, x, y)) return COL_WHITE;
        return COL_BLACK;
    }

    /* Footer bar: white text on red */
    if (y >= 236) {
        if (text_hit("BLE READY", centre_x("BLE READY", 3), 250, 3, x, y)) return COL_WHITE;
        return COL_RED;
    }

    /* Info block */
    if (text_hit("WFT0420CZ15", 24, 74, 2, x, y))  return COL_BLACK;
    if (text_hit("400X300 3-COLOUR", 24, 102, 2, x, y)) return COL_BLACK;
    if (text_hit("CTRL UC8176", 24, 130, 2, x, y)) return COL_BLACK;
    if (text_hit("FW 1.0", 24, 158, 2, x, y))     return COL_BLACK;
    if (text_hit("BLE EINK-N51802", 24, 186, 2, x, y)) return COL_RED;

    /* Red separator line above the footer */
    if (y >= 224 && y < 228) return COL_RED;

    return COL_WHITE;
}

static void stream_pattern(void) {
    uint32_t cmd_bw  = (eink_variant == EINK_V2) ? 0x24 : 0x10;
    uint32_t cmd_red = (eink_variant == EINK_V2) ? 0x26 : 0x13;

    /* Black plane: 1 = not black (white or red). */
    cmd((uint8_t)cmd_bw);
    for (uint32_t y = 0; y < EPD_HEIGHT; y++) {
        for (uint32_t i = 0; i < EPD_ROWBYTES; i++) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; bit++) {
                uint32_t x = i * 8 + (uint32_t)bit;
                if (screen_colour(x, y) != COL_BLACK) b |= (uint8_t)(1U << (7 - bit));
            }
            dat(b);
        }
    }
    /* Red plane: 1 = red, sent inverted (0xFF = no red). */
    cmd((uint8_t)cmd_red);
    for (uint32_t y = 0; y < EPD_HEIGHT; y++) {
        for (uint32_t i = 0; i < EPD_ROWBYTES; i++) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; bit++) {
                uint32_t x = i * 8 + (uint32_t)bit;
                if (screen_colour(x, y) == COL_RED) b |= (uint8_t)(1U << (7 - bit));
            }
            dat((uint8_t)~b);
        }
    }
}

void eink_show_test_pattern(void) {
    if (eink_variant == EINK_V2) {
        eink_reset();
        init_v2();
    } else {
        init_v1();          /* includes the stock three-pulse reset */
    }
    stream_pattern();
    turn_on_display();
}

void eink_sleep(void) {
    /* The shutdown tail (0x02 power off, 0x07 deep sleep) invalidates the
     * controller RAM, which partial updates rely on. When keep-awake is
     * requested the panel stays initialised so windowed writes work;
     * otherwise it sleeps like the stock firmware does. */
    if (!s_keep_awake) {
        s_panel_ready = 0;
        if (eink_variant == EINK_V2) {
            cmd(0x10);
            dat(0x01);
            delay_ms(100);
        } else {
            epd_all_idle();
        }
    }
}

/* ---- streaming API (content over BLE) -------------------------------
 * The controller keeps its write pointer while CS toggles, so bytes can
 * arrive at any pace: each BLE write is streamed straight through.
 * A full frame is 15,000 bytes per plane; a partial update streams only
 * the pixels of the requested rectangle (see eink_partial_begin).
 */
static uint32_t m_plane_bytes;      /* bytes expected for the open plane */
void eink_stream_begin(void) {
    if (!s_panel_ready) {
        if (eink_variant == EINK_V2) {
            eink_reset();
            init_v2();
        } else {
            init_v1();
        }
        s_panel_ready = 1;
    }
    m_plane_bytes = (uint32_t)EPD_ROWBYTES * EPD_HEIGHT;
    cmd((eink_variant == EINK_V2) ? 0x24 : 0x10);
}

/* Window the controller's RAM to a rectangle, then open the black plane.
 * Commands: 0x44 = X start/end (in bytes, i.e. pixels/8), 0x45 = Y
 * start/end (in pixels, 9 bits each), 0x4E/0x4F = cursors.
 * The panel still needs a full refresh afterwards (the red pigment has no
 * partial waveform), but only the rectangle has to travel over BLE. */
int eink_partial_begin_checked(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    /* A window write only refreshes the pixels inside the rectangle; the
     * rest of the controller RAM must still hold the previous frame.
     * That is only true while the panel has stayed initialised and
     * powered - after 0x07 deep sleep the RAM is not guaranteed. If we
     * cannot guarantee it, refuse and let the caller send a full frame.
     */
    if (!s_panel_ready) return 0;
    if (x >= EPD_WIDTH || y >= EPD_HEIGHT) return 0;
    if (x + w > EPD_WIDTH) w = EPD_WIDTH - x;
    if (y + h > EPD_HEIGHT) h = EPD_HEIGHT - y;
    if (w == 0 || h == 0) return 0;

    cmd(0x44);
    dat((uint8_t)(x / 8));
    dat((uint8_t)((x + w - 1) / 8));
    cmd(0x45);
    dat((uint8_t)(y & 0xFF));
    dat((uint8_t)(y >> 8));
    dat((uint8_t)((y + h - 1) & 0xFF));
    dat((uint8_t)((y + h - 1) >> 8));
    cmd(0x4E);
    dat((uint8_t)(x / 8));
    cmd(0x4F);
    dat((uint8_t)(y & 0xFF));
    dat((uint8_t)(y >> 8));

    m_plane_bytes = ((w + 7) / 8) * h;
    cmd((eink_variant == EINK_V2) ? 0x24 : 0x10);
    return 1;
}

void eink_stream_byte(uint8_t b) {
    dat(b);
}

void eink_stream_begin_red(void) {
    cmd((eink_variant == EINK_V2) ? 0x26 : 0x13);
}

void eink_stream_end(void) {
    turn_on_display();
}

uint32_t eink_plane_bytes(void) {
    if (m_plane_bytes == 0) return (uint32_t)EPD_ROWBYTES * EPD_HEIGHT;
    return m_plane_bytes;
}

/* ---- non-blocking refresh, used by the BLE path ---------------------
 * The panel needs ~20 s after the refresh command, and blocking the main
 * loop that long would starve the SoftDevice and drop the link. So the
 * refresh is split: start it, let the caller keep servicing BLE, then
 * finish the shutdown once the panel had its time. */
void eink_refresh_start(void) {
    if (eink_variant == EINK_V2) {
        cmd(0x22);
        dat(0xF7);
        cmd(0x20);
    } else {
        cmd(0x12);
    }
}

void eink_refresh_finish(void) {
    if (eink_variant == EINK_V2) {
        cmd(0x10);
        dat(0x01);
        delay_ms(100);
    } else {
        cmd(0x50);
        dat(0xF7);
        delay_ms(100);
        cmd(0x02);
        delay_ms(500);
        cmd(0x07);
        dat(0xA5);
        delay_ms(300);
    }
    epd_all_idle();
}

void eink_partial_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!eink_partial_begin_checked(x, y, w, h)) {
        eink_stream_begin();        /* fall back to a full frame */
    }
}
