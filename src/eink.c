/* e-paper front end.
 *
 * The panel itself is chosen at run time (see panel.h); everything here
 * works off the active panel's geometry so adding a panel needs no
 * changes in this file beyond a screen layout that suits its aspect.
 *
 * Rendering is procedural: pixels are computed on the fly while the
 * bit-planes are streamed into the controller, so no frame buffer is
 * needed (this MCU only has 16 KB RAM and a 400x300 plane alone is
 * 15,000 bytes).
 *
 * Wire encoding, identical for every supported controller:
 *   black plane  bit 1 = no black (white or red)
 *   red plane    bit 1 = no red   (white or black)
 */
#include "eink.h"
#include "panel.h"
#include "nrf51_min.h"

/* ---- registry ------------------------------------------------------- */
extern const panel_t panel_wft_uc8176;
extern const panel_t panel_wft_uc8179;
extern const panel_t panel_hink_ssd1680;
extern const panel_t panel_hink_uc8151;
extern const panel_t panel_hink_e0213a22;

static const panel_t *const m_panels[PANEL_COUNT] = {
    &panel_wft_uc8176,
    &panel_wft_uc8179,
    &panel_hink_ssd1680,
    &panel_hink_uc8151,
    &panel_hink_e0213a22,
};

static int m_panel_index = CFG_PANEL_DEFAULT;

const panel_t *panel_get(int index) {
    if (index < 0 || index >= PANEL_COUNT) return 0;
    return m_panels[index];
}

const panel_t *panel_active(void) { return m_panels[m_panel_index]; }
int panel_index(void) { return m_panel_index; }

void panel_select(int index) {
    if (index >= 0 && index < PANEL_COUNT) m_panel_index = index;
}

/* ---- diagnostics (mirrored into the descriptors' counters) ---------- */
volatile uint32_t eink_busy_polls;
volatile uint32_t eink_busy_seen;
volatile uint32_t eink_busy_high_samples;
volatile uint32_t eink_refresh_wait_ms;
volatile uint32_t eink_last_status;
volatile uint32_t eink_busy_low_samples;
volatile uint32_t eink_busy_first_ms;
volatile uint32_t eink_busy_release_ms;
volatile uint32_t eink_busy_used;

static int s_panel_ready;
static int m_in_red;
static uint32_t m_plane_bytes;

void eink_set_keep_awake(int on) { (void)on; }
int eink_panel_ready(void) { return s_panel_ready; }

static void sync_diagnostics(void) {
    eink_busy_high_samples = epd_busy_samples;
    eink_busy_first_ms = epd_busy_first_ms;
    eink_busy_release_ms = epd_busy_release_ms;
    eink_busy_used = epd_busy_used;
}

/* ---- lifecycle ------------------------------------------------------ */
void eink_gpio_init(void) { epd_bus_init(); }

void eink_init(void) {
    panel_active()->init();
    s_panel_ready = 1;
}

void eink_refresh_start(void) {
    uint32_t t0 = rtc_ms();
    panel_active()->refresh_start();
    eink_refresh_wait_ms = rtc_ms() - t0;
}

void eink_refresh_finish(void) {
    uint32_t t0 = rtc_ms();
    panel_active()->refresh_finish();
    sync_diagnostics();
    eink_refresh_wait_ms = rtc_ms() - t0;
    s_panel_ready = 0;
}

void eink_sleep(void) {
    panel_active()->deep_sleep();
    s_panel_ready = 0;
}

/* Is the panel still updating? Only meaningful on parts whose BUSY line is
 * actually wired; when it is not, a loose 0 means "not busy" and the caller
 * falls back to its own deadline. */
int eink_panel_busy(void) {
    return epd_busy_asserted(panel_active()->busy_active_high);
}

/* Finish a refresh that was started earlier: power the panel down without
 * waiting. Used by the cooperative path so BLE keeps being serviced. */
void eink_refresh_complete(void) {
    panel_active()->deep_sleep();
    sync_diagnostics();
    s_panel_ready = 0;
}

uint32_t eink_plane_bytes(void) {
    const panel_t *p = panel_active();
    uint32_t full = panel_plane_bytes(p);
    if (m_plane_bytes == 0 || m_plane_bytes > full) return full;
    return m_plane_bytes;
}

uint32_t eink_refresh_ms(void) { return panel_active()->refresh_ms; }

/* ---- text rendering ------------------------------------------------- */
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

static int text_px(const char *s, int scale) {
    int n = 0;
    while (s[n]) n++;
    return n * 6 * scale;
}

/* Horizontal start for centred text inside the active panel. */
static int centre_x(const char *s, int scale) {
    return (panel_active()->width - text_px(s, scale)) / 2;
}

static int text_centred(const char *s, int ty, int scale, uint32_t x, uint32_t y) {
    return text_hit(s, centre_x(s, scale), ty, scale, x, y);
}

/* ---- boot screen: 4.2" landscape (400x300) ------------------------- */
static int screen_wide(uint32_t x, uint32_t y) {
    const panel_t *p = panel_active();
    const int frame = 3;

    if (x < (uint32_t)frame || x >= (uint32_t)(p->width - frame) ||
        y < (uint32_t)frame || y >= (uint32_t)(p->height - frame)) {
        return COL_BLACK;
    }
    if (y < 58) {
        if (text_hit("EINK-N51802", 56, 20, 3, x, y)) return COL_WHITE;
        return COL_BLACK;
    }
    if (y >= 236) {
        if (text_centred("BLE READY", 250, 3, x, y)) return COL_WHITE;
        return COL_RED;
    }
    if (text_hit(p->name, 24, 74, 2, x, y))       return COL_BLACK;
    if (text_hit("400X300 3-COLOUR", 24, 102, 2, x, y)) return COL_BLACK;
    if (text_hit("CTRL", 24, 130, 2, x, y))       return COL_BLACK;
    if (text_hit(p->ctrl, 84, 130, 2, x, y))      return COL_BLACK;
    if (text_hit("FW 2.0", 24, 158, 2, x, y))     return COL_BLACK;
    if (text_hit("BLE EINK-N51802", 24, 186, 2, x, y)) return COL_RED;

    if (y >= 224 && y < 228) return COL_RED;
    return COL_WHITE;
}

/* ---- boot screen: 2.9" portrait (128x296) -------------------------- */
static int screen_tall(uint32_t x, uint32_t y) {
    const panel_t *p = panel_active();
    const int H = (int)p->height;
    const int frame = 3;
    /* Layout was designed for 296 rows; scale it for shorter panels. */
    #define YS(v) ((v) * H / 296)

    if (x < (uint32_t)frame || x >= (uint32_t)(p->width - frame) ||
        y < (uint32_t)frame || y >= (uint32_t)(H - frame)) {
        return COL_BLACK;
    }
    if ((int)y < YS(52)) {
        if (text_centred("EINK-N51802", YS(12), 1, x, y)) return COL_WHITE;
        if (text_centred(p->name, YS(30), 1, x, y))      return COL_WHITE;
        return COL_BLACK;
    }
    if ((int)y >= YS(246)) {
        if (text_centred("EINK TAG", YS(262), 2, x, y)) return COL_WHITE;
        return COL_RED;
    }
    if (text_hit("3-COLOUR", 6, YS(66), 1, x, y))    return COL_BLACK;
    if (text_hit("CTRL", 6, YS(86), 1, x, y))        return COL_BLACK;
    if (text_hit(p->ctrl, 36, YS(86), 1, x, y))      return COL_BLACK;
    if (text_hit("FW 2.0", 6, YS(106), 1, x, y))     return COL_BLACK;
    if (text_hit("BLE 57600", 6, YS(126), 1, x, y))  return COL_BLACK;

    if ((int)y >= YS(152) && (int)y < YS(156)) return COL_RED;

    if (text_hit("BLE READY", 6, YS(168), 2, x, y))  return COL_RED;

    if (text_hit("PAIR WITH", 6, YS(210), 1, x, y))  return COL_BLACK;
    if (text_hit("EINK-N51802", 6, YS(228), 1, x, y)) return COL_BLACK;

    #undef YS
    return COL_WHITE;
}

static int screen_colour(uint32_t x, uint32_t y) {
    const panel_t *p = panel_active();
    if (x >= p->width) return COL_WHITE;   /* spare bits in the last byte */
    if (p->width >= 300) return screen_wide(x, y);
    return screen_tall(x, y);
}

/* ---- streaming ------------------------------------------------------ */
#ifdef EPD_DIAG_BANDS
/* Diagnostic: four equal horizontal bands that exercise every
 * (black-plane bit, red-plane bit) combination, top to bottom:
 *   band 0: (1,1)   band 1: (0,1)   band 2: (1,0)   band 3: (0,0)
 * Which colour each band shows tells us the panel's LUT mapping. */
static void stream_pattern(void) {
    const panel_t *p = panel_active();
    const uint32_t rowbytes = panel_row_bytes(p);
    const uint32_t band_h = p->height / 4;

    p->open_bw();
    for (uint32_t y = 0; y < p->height; y++) {
        uint32_t band = (y / band_h > 3) ? 3 : y / band_h;
        uint8_t v = (band == 0 || band == 2) ? 0xFF : 0x00;
        for (uint32_t i = 0; i < rowbytes; i++) epd_dat(v);
    }
    p->open_red();
    for (uint32_t y = 0; y < p->height; y++) {
        uint32_t band = (y / band_h > 3) ? 3 : y / band_h;
        uint8_t v = (band == 0 || band == 1) ? 0xFF : 0x00;
        for (uint32_t i = 0; i < rowbytes; i++) epd_dat(v);
    }
}
#else
static void stream_pattern(void) {
    const panel_t *p = panel_active();
    const uint32_t rowbytes = panel_row_bytes(p);

    p->open_bw();
    for (uint32_t y = 0; y < p->height; y++) {
        for (uint32_t i = 0; i < rowbytes; i++) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; bit++) {
                uint32_t x = i * 8 + (uint32_t)bit;
                if (screen_colour(x, y) != COL_BLACK) b |= (uint8_t)(1U << (7 - bit));
            }
            epd_dat(b);
        }
    }

    if (p->planes < 2) return;

    p->open_red();
    for (uint32_t y = 0; y < p->height; y++) {
        for (uint32_t i = 0; i < rowbytes; i++) {
            uint8_t b = 0;
            for (int bit = 0; bit < 8; bit++) {
                uint32_t x = i * 8 + (uint32_t)bit;
                if (screen_colour(x, y) == COL_RED) b |= (uint8_t)(1U << (7 - bit));
            }
            /* Bit 1 means "red"; panels that want "no red" get it inverted. */
            epd_dat(p->red_one ? b : (uint8_t)~b);
        }
    }
}
#endif

void eink_show_test_pattern(void) {
    uint32_t t0;
    eink_init();
    stream_pattern();
    t0 = rtc_ms();
    panel_active()->refresh_start();
    panel_active()->refresh_finish();
    eink_refresh_wait_ms = rtc_ms() - t0;
    sync_diagnostics();
    s_panel_ready = 0;
}

static void stream_all(uint8_t value) {
    const panel_t *p = panel_active();
    const uint32_t bytes = panel_plane_bytes(p);
    uint8_t red_none = p->red_one ? 0x00 : 0xFF;   /* "no red" in that encoding */

    p->open_bw();
    for (uint32_t i = 0; i < bytes; i++) epd_dat(value);
    if (p->planes < 2) return;
    p->open_red();
    for (uint32_t i = 0; i < bytes; i++) epd_dat(red_none);
}

void eink_clear(void) {
    stream_all(0xFF);              /* 1 = white in both planes */
    panel_active()->refresh_start();
    panel_active()->refresh_finish();
    sync_diagnostics();
    s_panel_ready = 0;
}

/* ---- streaming API (content delivered over BLE) --------------------- */
void eink_stream_begin(void) {
    const panel_t *p = panel_active();
    if (!s_panel_ready) eink_init();
    m_plane_bytes = panel_plane_bytes(p);
    m_in_red = 0;
    p->open_bw();
}

/* The host always sends logical bits (black plane: 1 = no black, red
 * plane: 1 = red). Panels that want the red plane inverted get it here,
 * so one host tool works for every panel. */
void eink_stream_byte(uint8_t b) {
    const panel_t *p = panel_active();
    if (m_in_red && p->planes >= 2 && !p->red_one) b = (uint8_t)~b;
    epd_dat(b);
}

void eink_stream_begin_red(void) {
    panel_active()->open_red();
    m_in_red = 1;
}

void eink_stream_end(void) {
    panel_active()->refresh_start();
    panel_active()->refresh_finish();
    sync_diagnostics();
    s_panel_ready = 0;
}

/* Windowed partial updates are not supported by any of these panels in
 * the tri-colour configuration (the red pigment has no partial waveform),
 * so these exist only so older callers still link. */
int eink_partial_begin_checked(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x; (void)y; (void)w; (void)h;
    return 0;
}

void eink_partial_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)x; (void)y; (void)w; (void)h;
    eink_stream_begin();
}
