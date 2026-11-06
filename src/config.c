/* Persistent configuration - see config.h for the design notes. */
#include "config.h"

#include "nrf51_min.h"
#include "nrf_soc.h"
#include "nrf_error.h"

static cfg_t m_cfg;
static int m_from_flash;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return ~crc;
}

void cfg_defaults(void) {
    static const char dflt[] = "EINK-N51802";
    m_cfg.magic = CFG_MAGIC;
    m_cfg.version = CFG_VERSION;
    m_cfg.adv_timeout_s = 5;
    m_cfg.idle_rounds = 6;
    m_cfg.wake_enable = 0;
    m_cfg.boot_refresh = 1;
    m_cfg.flags = 0;
    for (uint32_t i = 0; i < 6; i++) m_cfg.addr[i] = 0;
    m_cfg.addr_set = 0;
    m_cfg.addr_pad[0] = m_cfg.addr_pad[1] = 0;
    for (uint32_t i = 0; i < CFG_NAME_LEN; i++) {
        m_cfg.name[i] = (i < (sizeof(dflt) - 1U)) ? dflt[i] : 0;
    }
    m_cfg.crc = crc32((const uint8_t *)&m_cfg, sizeof(cfg_t) - sizeof(uint32_t));
}

static int cfg_valid(const cfg_t *c) {
    if (c->magic != CFG_MAGIC) return 0;
    if (c->version != CFG_VERSION) return 0;
    if (c->adv_timeout_s == 0 || c->adv_timeout_s > 180) return 0;
    if (c->idle_rounds > 600) return 0;
    if (c->name[CFG_NAME_LEN - 1] != 0) return 0;
    if (c->name[0] == 0) return 0;
    uint32_t want = crc32((const uint8_t *)c, sizeof(cfg_t) - sizeof(uint32_t));
    return want == c->crc;
}

const cfg_t *cfg_init(void) {
    const cfg_t *flash = (const cfg_t *)CFG_PAGE_ADDR;
    if (cfg_valid(flash)) {
        m_cfg = *flash;
        m_from_flash = 1;
    } else {
        cfg_defaults();
        m_from_flash = 0;
    }
    return &m_cfg;
}

cfg_t *cfg_live(void) { return &m_cfg; }

int cfg_from_flash(void) { return m_from_flash; }

/* The SoftDevice flash API is asynchronous: sd_flash_page_erase() and
 * sd_flash_write() return as soon as the operation is queued, and the
 * result arrives later as an SoC event. Verifying the page immediately
 * after the call therefore reads stale data - which is exactly what the
 * first version did (cfg_save reported failure although the write was
 * still in flight). Wait for the completion event here. */
static int wait_flash_event(uint32_t timeout_ms) {
    uint32_t t0 = rtc_ms();
    for (;;) {
        uint32_t evt;
        while (sd_evt_get(&evt) == NRF_SUCCESS) {
            if (evt == NRF_EVT_FLASH_OPERATION_SUCCESS) return 1;
            if (evt == NRF_EVT_FLASH_OPERATION_ERROR) return 0;
        }
        if ((rtc_ms() - t0) >= timeout_ms) return 0;
        delay_ms(1);
    }
}

static uint32_t erase_page(uint32_t page_addr) {
    uint32_t err;
    do {
        err = sd_flash_page_erase(page_addr / CFG_PAGE_SIZE);
    } while (err == NRF_ERROR_BUSY);
    if (err != NRF_SUCCESS) return err;
    return wait_flash_event(200) ? NRF_SUCCESS : 0xFFFFFFFEUL;
}

static uint32_t write_words(uint32_t dst, const uint32_t *src, uint32_t words) {
    uint32_t *d = (uint32_t *)dst;
    while (words) {
        uint32_t n = (words > 64U) ? 64U : words;   /* keep chunks small */
        uint32_t err;
        do {
            err = sd_flash_write(d, src, n);
        } while (err == NRF_ERROR_BUSY);
        if (err != NRF_SUCCESS) return err;
        d += n;
        src += n;
        words -= n;
    }
    return NRF_SUCCESS;
}

uint32_t cfg_save(void) {
    m_cfg.magic = CFG_MAGIC;
    m_cfg.version = CFG_VERSION;
    m_cfg.crc = crc32((const uint8_t *)&m_cfg, sizeof(cfg_t) - sizeof(uint32_t));

    uint32_t err = erase_page(CFG_PAGE_ADDR);
    if (err != NRF_SUCCESS) return 0x10000000UL | (err & 0xFFUL);

    uint32_t words = (sizeof(cfg_t) + 3U) / 4U;
    err = write_words(CFG_PAGE_ADDR, (const uint32_t *)&m_cfg, words);
    if (err != NRF_SUCCESS) return 0x20000000UL | (err & 0xFFUL);

    /* Let the flash controller settle before verifying: reading straight
     * after the completion event has been seen returning stale data on
     * this part. */
    delay_ms(20);

    const cfg_t *back = (const cfg_t *)CFG_PAGE_ADDR;
    if (back->magic != CFG_MAGIC) return 0x30000001UL;
    if (back->version != CFG_VERSION) return 0x30000002UL;
    if (!cfg_valid(back)) return 0x30000003UL;
    return NRF_SUCCESS;
}

static int streq(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint32_t parse_u32(const char *s, uint32_t n, uint32_t *out) {
    uint32_t v = 0;
    if (n == 0) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10U + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return 1;
}

static int parse_addr(const char *s, uint32_t n);

int cfg_apply_text(const char *s, uint32_t len) {
    /* strip trailing CR/LF */
    while (len && (s[len - 1] == '\r' || s[len - 1] == '\n')) len--;

    if (streq(s, "reset", 5) && len == 5) {
        cfg_defaults();
        return 1;
    }

    uint32_t eq = 0;
    while (eq < len && s[eq] != '=') eq++;
    if (eq == 0 || eq >= len) return 0;
    const char *val = s + eq + 1;
    uint32_t vlen = len - eq - 1;

    if (streq(s, "name", 4) && eq == 4) {
        if (vlen == 0 || vlen > CFG_NAME_LEN - 1) return -1;
        for (uint32_t i = 0; i < CFG_NAME_LEN; i++) m_cfg.name[i] = 0;
        for (uint32_t i = 0; i < vlen; i++) m_cfg.name[i] = val[i];
        return 1;
    }
    if (streq(s, "adv", 3) && eq == 3) {
        uint32_t v;
        if (!parse_u32(val, vlen, &v) || v == 0 || v > 180) return -1;
        m_cfg.adv_timeout_s = (uint16_t)v;
        return 1;
    }
    if (streq(s, "idle", 4) && eq == 4) {
        uint32_t v;
        if (!parse_u32(val, vlen, &v) || v > 600) return -1;
        m_cfg.idle_rounds = (uint16_t)v;
        return 1;
    }
    if (streq(s, "addr", 4) && eq == 4) {
        return parse_addr(val, vlen);
    }
    if (streq(s, "boot", 4) && eq == 4) {
        uint32_t v;
        if (!parse_u32(val, vlen, &v) || v > 1) return -1;
        m_cfg.boot_refresh = (uint8_t)v;
        return 1;
    }
    if (streq(s, "wake", 4) && eq == 4) {
        uint32_t v;
        if (!parse_u32(val, vlen, &v) || v > 1) return -1;
        m_cfg.wake_enable = (uint8_t)v;
        return 1;
    }
    return 0;
}
/* "addr=AA:BB:CC:DD:EE:FF" (big-endian text, stored little-endian) or
 * "addr=auto" to fall back to the FICR factory address. */
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_addr(const char *s, uint32_t n) {
    if (n == 4 && streq(s, "auto", 4)) {
        m_cfg.addr_set = 0;
        for (uint32_t i = 0; i < 6; i++) m_cfg.addr[i] = 0;
        return 1;
    }
    if (n != 17) return -1;
    uint8_t b[6];
    for (uint32_t i = 0; i < 6; i++) {
        int hi = hex_nibble(s[i * 3]);
        int lo = hex_nibble(s[i * 3 + 1]);
        if (hi < 0 || lo < 0) return -1;
        if (i < 5 && s[i * 3 + 2] != ':' && s[i * 3 + 2] != '-') return -1;
        b[i] = (uint8_t)((hi << 4) | lo);
    }
    /* text is MSB first, BLE wants LSB first */
    for (uint32_t i = 0; i < 6; i++) m_cfg.addr[i] = b[5 - i];
    m_cfg.addr_set = 1;
    return 1;
}