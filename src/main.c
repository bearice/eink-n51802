/* nRF51802 e-ink tag: boot -> information screen -> BLE advertising.
 *
 * Boot flow:
 *   1. HFCLK/RTC1 time base, hardware UART TX=P8 (57600 8N1)
 *   2. E-ink: reset, init, stream the information screen, refresh, sleep
 *   3. SoftDevice S110: enable + GAP advertising ("EINK-N51802")
 *   4. Loop on SoftDevice events. After six silent advertising intervals
 *      the firmware enters SYSTEM OFF with P9 armed as a sense-high wake
 *      input, so a sufficiently strong RF field coupled into the coil
 *      resets the chip and restarts this sequence.
 *
 * The P9 circuit is a passive RF coupling input: coil -> coupling
 * capacitor -> P9, coil -> GND, P9 -> 150 kOhm pull-down. It can sense
 * the field; it cannot receive NFC protocol frames.
 */
#include "nrf51_min.h"
#include "hwuart.h"
#include "eink.h"
#include "config.h"

#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "ble.h"
#include "ble_gap.h"
#include "ble_gatts.h"
#include "nrf_error.h"
#include "ble_hci.h"

/* ---------- debug log (software UART on P8) ---------- */
static void uart_init(void) {
    hwuart_init();
}

/* ---------- LEDs (P3-P5, active low) ---------- */
static void led_init(void) {
    gpio_dir_output(PIN_LED1);
    gpio_dir_output(PIN_LED2);
    gpio_dir_output(PIN_LED3);
    gpio_write(PIN_LED1, 1);      /* off */
    gpio_write(PIN_LED2, 1);
    gpio_write(PIN_LED3, 1);
}
static void led_set(uint32_t pin, int on) { gpio_write(pin, on ? 0 : 1); }


/* ---------- RF/NFC-coil wake on P9 ----------
 * The external circuit is a coupling capacitor to the coil(s), the other
 * end of the coil grounded, and a 150 kOhm pull-down on P9. The nRF51802
 * cannot decode NFC, but SYSTEM OFF retains GPIO SENSE configuration:
 * a positive RF excursion that crosses the input threshold wakes the MCU
 * and restarts it as a normal reset.
 *
 * DISABLED BY DEFAULT. Measured on this board, a phone touching the coil
 * only produces ~0.5 V at P9, while the nRF51 digital input threshold is
 * 0.7 x VDD = 2.31 V. With SYSTEM OFF armed the tag would sleep forever
 * and need a power cycle (or SWD) to come back, so the firmware keeps
 * advertising instead. Enable with -DENABLE_P9_WAKE=1 once the coil is
 * tuned (resonant capacitor) or a detector/comparator is fitted.
 */
#ifndef ENABLE_P9_WAKE
#define ENABLE_P9_WAKE 1   /* capability; the config flag decides */
#endif

static uint32_t m_idle_advertising_rounds;

static void wake_input_init(void) {
    /* P9 has no defined DC level on this board: the coupling capacitor
     * blocks DC, so the external 150 kOhm pull-down does not set the pin
     * (measured: it floats, sometimes high, sometimes low). Without a
     * defined level edge detection is meaningless - a manual short to VCC
     * produced no edge at all because the pin was already high.
     * The internal pull-down fixes that: idle = low, and the coil's
     * positive pulse (or a short to VCC) gives the rising edge the stock
     * firmware expects with its SENSE = high. */
    gpio_dir_input(PIN_WAKE, CFG_INPUT_SENSE_HIGH_PD);
}

/* Runtime detection of the RF field on P9 (option A): SENSE -> DETECT ->
 * GPIOTE PORT event -> this interrupt. The handler only records the event;
 * the main loop reacts, because restarting advertising must not happen in
 * interrupt context. */
static volatile uint32_t m_p9_events;

void GPIOTE_IRQHandler(void) {
    REG(GPIOTE_BASE, GPIOTE_EVENTS_PORT) = 0;
    m_p9_events++;
}

static void p9_irq_init(void) {
    REG(GPIOTE_BASE, GPIOTE_POWER) = 1UL;
    REG(GPIOTE_BASE, GPIOTE_CONFIG(0)) =
        (GPIOTE_PSEL_PORT << 8) | GPIOTE_MODE_EVENT;
    REG(GPIOTE_BASE, GPIOTE_EVENTS_PORT) = 0UL;
    REG(GPIOTE_BASE, GPIOTE_INTENSET) = (1UL << 31);   /* PORT interrupt */
    (void)sd_nvic_ClearPendingIRQ(GPIOTE_IRQN);
    (void)sd_nvic_SetPriority(GPIOTE_IRQN, 3);
    (void)sd_nvic_EnableIRQ(GPIOTE_IRQN);
}

#if ENABLE_P9_WAKE
static void enter_system_off_for_wake(void) {
    hwuart_puts("BLE idle; P9 wake armed, system off\r\n");
    (void)sd_ble_gap_adv_stop();
    (void)sd_softdevice_disable();
    (void)sd_power_system_off();
    for (;;) { }
}
#endif

/* ---------- SoftDevice / BLE ---------- */
static const cfg_t *g_cfg;          /* live configuration (config.c) */

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Apply the parts of the configuration that the GAP layer owns. Called at
 * boot and after a config change; it also restarts advertising so the new
 * name / address actually reach the air. */
static void apply_gap_config(void) {
    static const ble_gap_conn_sec_mode_t open = { 0 };
    (void)sd_ble_gap_device_name_set(&open, (const uint8_t *)g_cfg->name,
                                     (uint16_t)str_len(g_cfg->name));
    if (g_cfg->addr_set) {
        ble_gap_addr_t a;
        for (int i = 0; i < 6; i++) a.addr[i] = g_cfg->addr[i];
        a.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
        (void)sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE, &a);
    }
}

static void sd_assert_handler(uint32_t pc, uint16_t line, const uint8_t *file) {
    (void)pc; (void)line; (void)file;
    /* Fatal stack error: reset. SCB AIRCR 0xE000ED0C, SYSRESETREQ. */
    *(volatile uint32_t *)0xE000ED0CUL = 0x05FA0004UL;
    while (1) { }
}

static void check(const char *what, uint32_t err) {
    if (err == NRF_SUCCESS) return;
    hwuart_puts("SD ERR ");
    hwuart_puts(what);
    hwuart_puts(" code=");
    hwuart_put_u32(err);
    hwuart_puts(" (0x");
    hwuart_put_hex32(err);
    hwuart_puts(")\nSLEEP\n");
    /* do not die silently: park with LEDs on */
    led_set(PIN_LED1, 1);
    led_set(PIN_LED2, 1);
    led_set(PIN_LED3, 1);
    while (1) { }
}

#define ADV_INTERVAL_UNITS 32       /* 32 * 0.625ms = 20ms */

static void start_advertising(void) {
    /* Adv payload: flags + manufacturer-specific (Nordic company id 0x0059,
     * payload = "EINK" + panel marker + SW version; the name lives in the
     * scan response). Length bytes must match the data that follows them:
     * flags 0x02 = type+1; manufacturer 0x0B = type + 10 payload bytes. */
    static const uint8_t adv_payload[] = {
        0x02, 0x01, 0x06,                         /* Flags: LE limited */
        0x0B, 0xFF, 0x59, 0x00,                   /* Manufacturer, 11 bytes w/ type */
        'E', 'I', 'N', 'K',                       /*  ... 4 chars */
        0x02, 0xB0,                               /*  ... panel marker WFT0420CZ15 */
        0x01, 0x01,                               /*  ... SW version 1.1 */
    };
    /* Scan response carries the configured name, built at run time so a
     * config change actually reaches the air. Layout: [len][0x09][name]. */
    static uint8_t scan_rsp[2 + 15];
    uint32_t nlen = str_len(g_cfg->name);
    if (nlen > 15) nlen = 15;
    scan_rsp[0] = (uint8_t)(nlen + 1);
    scan_rsp[1] = 0x09;                 /* complete local name */
    for (uint32_t i = 0; i < nlen; i++) scan_rsp[2 + i] = (uint8_t)g_cfg->name[i];

    ble_gap_adv_params_t adv_params = {
        .type          = BLE_GAP_ADV_TYPE_ADV_IND,
        .p_peer_addr   = 0,
        .fp            = BLE_GAP_ADV_FP_ANY,
        .p_whitelist   = 0,
        .interval      = ADV_INTERVAL_UNITS,
        .timeout       = g_cfg->adv_timeout_s,
        .channel_mask  = { 0, 0, 0 },
    };
    uint32_t err = sd_ble_gap_adv_data_set(adv_payload, (uint8_t)sizeof(adv_payload),
                                           scan_rsp, (uint8_t)(2 + nlen));
    if (err == NRF_SUCCESS) {
        err = sd_ble_gap_adv_start(&adv_params);
    }
    if (err != NRF_SUCCESS) {
        /* Restarting advertising while a central is connected is a normal
         * no-op, not a fatal error - the disconnect handler restarts it. */
        hwuart_puts("adv_start 0x");
        hwuart_put_hex32(err);
        hwuart_puts("\r\n");
    }
    led_set(PIN_LED2, 0);         /* LED2 off = not connected (just advertising) */
}

/* freestanding memset (no libc in this image) */
static void zero_bytes(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}

/* ---------- image transfer service ----------------------------------
 * Vendor-specific service 0xFFF0 with one write characteristic 0xFFF1
 * (write-without-response, up to 20 bytes per packet). Data is streamed
 * straight into the panel's RAM, so the 30,000 bytes of a full frame cost
 * no application RAM at all.
 *
 * Packet format (byte 0 = opcode, up to 19 payload bytes):
 *   0x01            begin session: panel reset+init, open black plane
 *   0x02 <19 bytes> image data for the current plane
 *   0x03            switch to the red plane
 *   0x04            end: refresh the panel (blocking, ~20 s)
 * After 15000 bytes the driver switches plane automatically, so a client
 * may simply stream 15000 + 15000 bytes and then send 0x04.
 */
#define IMG_OP_BEGIN 0x01
#define IMG_OP_DATA  0x02
#define IMG_OP_RED   0x03
#define IMG_OP_END   0x04
#define IMG_OP_RECT  0x05   /* partial update: x,y,w,h as u16 LE    */

static uint16_t m_img_handle;
static uint16_t m_cfg_handle;
static uint32_t m_img_state;      /* 0 idle, 1 black plane, 2 red plane, 3 refreshing */
static uint32_t m_img_count;      /* bytes pushed into the current plane  */
static uint32_t m_refresh_until;  /* rtc_ms() deadline while refreshing   */
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;

/* ---------- LED status display ----------
 *   LED1 (P3): 1 Hz blink while advertising, off once a central connects
 *   LED2 (P4): on = connected and idle, blinking = connected and
 *              transferring, off = not connected
 *   LED3 (P5): on while the panel is refreshing (boot or BLE triggered)
 * All of it is time-driven from the main loop, nothing blocks. */
static uint32_t m_bt_transfer;              /* image bytes arriving now */
static volatile uint32_t m_panel_busy;       /* boot refresh in progress */




static void led_tick(void) {
    uint32_t now = rtc_ms();

    int connected = (m_conn_handle != BLE_CONN_HANDLE_INVALID);

    if (!connected) {
        uint32_t lvl = ((now / 500U) & 1U);
        led_set(PIN_LED1, lvl ? 1 : 0);                   /* 1 Hz blink */

        led_set(PIN_LED2, 0);
    } else {
        led_set(PIN_LED1, 0);
        if (m_bt_transfer) led_set(PIN_LED2, ((now / 100U) & 1U) ? 1 : 0);
        else               led_set(PIN_LED2, 1);
    }
    led_set(PIN_LED3, (m_panel_busy || m_img_state == 3) ? 1 : 0);
}

#define IMG_REFRESH_MS 20000      /* UC8176 full 3-colour update */

static void img_finish(void) {
    if (m_img_state == 3) return;      /* already refreshing */
    /* Hand the refresh command to the panel and return immediately: the
     * main loop keeps servicing BLE for IMG_REFRESH_MS, then shuts the
     * panel down. Blocking here would starve the SoftDevice.
     * The link is closed cleanly right away - a panel refresh takes ~20 s
     * and the central would otherwise drop us on supervision timeout
     * (S110 rejects sd_ble_gap_ppcp_set on this chip, so the timeout is
     * whatever the central picked, typically 4 s). */
    eink_refresh_start();
    m_bt_transfer = 0;
    m_img_state = 3;
    m_refresh_until = rtc_ms() + IMG_REFRESH_MS;
    led_set(PIN_LED3, 1);         /* LED3 on while the panel refreshes */
    hwuart_puts("IMG refresh started\r\n");
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        (void)sd_ble_gap_disconnect(m_conn_handle,
                                    BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    }
}

static void img_switch_red(void) {
    eink_stream_begin_red();
    m_img_state = 2;
    m_img_count = 0;
}

static void img_write(const uint8_t *p, uint16_t len) {
    if (len == 0) return;
    switch (p[0]) {
    case IMG_OP_BEGIN:
        m_img_count = 0;
        m_bt_transfer = 1;
        eink_stream_begin();
        m_img_state = 1;
        hwuart_puts("IMG begin\r\n");
        break;
    case IMG_OP_DATA:
        if (m_img_state == 0) break;            /* not started yet */
        for (uint16_t i = 1; i < len; i++) {
            eink_stream_byte(p[i]);
            if (++m_img_count >= eink_plane_bytes()) {
                if (m_img_state == 1) img_switch_red();
                else { img_finish(); break; }
            }
        }
        break;
    case IMG_OP_RECT:
        /* Partial update: 8 bytes of geometry (x, y, w, h as u16 LE) after
         * the opcode. Only that rectangle travels over BLE; the panel still
         * does a full refresh afterwards. */
        if (len >= 9) {
            uint32_t x = (uint32_t)p[1] | ((uint32_t)p[2] << 8);
            uint32_t y = (uint32_t)p[3] | ((uint32_t)p[4] << 8);
            uint32_t w = (uint32_t)p[5] | ((uint32_t)p[6] << 8);
            uint32_t h = (uint32_t)p[7] | ((uint32_t)p[8] << 8);
            m_img_count = 0;
            m_bt_transfer = 1;
            eink_partial_begin(x, y, w, h);
            m_img_state = 1;
            hwuart_puts("IMG rect ");
            hwuart_put_u32(x); hwuart_puts(",");
            hwuart_put_u32(y); hwuart_puts(" ");
            hwuart_put_u32(w); hwuart_puts("x");
            hwuart_put_u32(h); hwuart_puts("\r\n");
        }
        break;
    case IMG_OP_RED:
        if (m_img_state == 1) img_switch_red();
        break;
    case IMG_OP_END:
        if (m_img_state != 0) img_finish();
        break;
    default:
        break;
    }
}

static void gatt_init(void) {
    ble_uuid_t svc_uuid;
    svc_uuid.type = BLE_UUID_TYPE_BLE;
    svc_uuid.uuid = 0xFFF0;
    uint16_t svc_handle;
    check("svc_add", sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
                                              &svc_uuid, &svc_handle));

    ble_uuid_t chr_uuid;
    chr_uuid.type = BLE_UUID_TYPE_BLE;
    chr_uuid.uuid = 0xFFF1;

    ble_gatts_attr_md_t attr_md;
    zero_bytes(&attr_md, sizeof(attr_md));
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr_md.vlen = 1;

    ble_gatts_char_md_t char_md;
    zero_bytes(&char_md, sizeof(char_md));
    char_md.char_props.write = 1;
    char_md.char_props.write_wo_resp = 1;

    ble_gatts_attr_t attr;
    zero_bytes(&attr, sizeof(attr));
    attr.p_attr_md = &attr_md;
    attr.p_uuid = &chr_uuid;
    attr.init_len = 1;
    attr.init_offs = 0;
    attr.max_len = 20;
    static uint8_t init_val = 0;
    attr.p_value = &init_val;

    ble_gatts_char_handles_t handles;
    check("chr_add", sd_ble_gatts_characteristic_add(svc_handle, &char_md,
                                                     &attr, &handles));
    m_img_handle = handles.value_handle;

    /* Second characteristic: text config commands (write) */
    ble_uuid_t cfg_uuid;
    cfg_uuid.type = BLE_UUID_TYPE_BLE;
    cfg_uuid.uuid = 0xFFF2;
    ble_gatts_char_md_t cmd_md;
    zero_bytes(&cmd_md, sizeof(cmd_md));
    cmd_md.char_props.write = 1;
    cmd_md.char_props.write_wo_resp = 1;
    ble_gatts_attr_t cmd_attr;
    zero_bytes(&cmd_attr, sizeof(cmd_attr));
    cmd_attr.p_attr_md = &attr_md;
    cmd_attr.p_uuid = &cfg_uuid;
    cmd_attr.init_len = 1;
    cmd_attr.init_offs = 0;
    cmd_attr.max_len = 20;
    static uint8_t cmd_val = 0;
    cmd_attr.p_value = &cmd_val;
    ble_gatts_char_handles_t cmd_handles;
    check("cfg_chr", sd_ble_gatts_characteristic_add(svc_handle, &cmd_md,
                                                     &cmd_attr, &cmd_handles));
    m_cfg_handle = cmd_handles.value_handle;
}

/* Handle one config command from characteristic 0xFFF2. */
static void cfg_write(const uint8_t *p, uint16_t len) {
    char buf[24];
    if (len > sizeof(buf) - 1) len = (uint16_t)(sizeof(buf) - 1);
    for (uint16_t i = 0; i < len; i++) buf[i] = (char)p[i];
    buf[len] = 0;

    if (len == 4 && buf[0] == 's' && buf[1] == 'a' && buf[2] == 'v' && buf[3] == 'e') {
        uint32_t err = cfg_save();
        if (err == 0) {
            hwuart_puts("CFG saved\r\n");
        } else {
            hwuart_puts("CFG save FAILED 0x");
            hwuart_put_hex32(err);
            hwuart_puts("\r\n");
        }
        return;
    }
    int r = cfg_apply_text(buf, len);
    if (r < 0) {
        hwuart_puts("CFG bad value\r\n");
        return;
    }
    if (r == 0) {
        hwuart_puts("CFG unknown key\r\n");
        return;
    }
    hwuart_puts("CFG ");
    hwuart_puts(buf);
    hwuart_puts("\r\n");
    apply_gap_config();
    /* Re-advertise so name/address changes are visible immediately -
     * but only when idle: while connected a restart is a no-op and the
     * disconnect handler will pick the new values up anyway. */
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID) {
        (void)sd_ble_gap_adv_stop();
        start_advertising();
    }
}

static void ble_init(void) {
    check("vtbase", sd_softdevice_vector_table_base_set(0x18000UL));
    check("sd_en", sd_softdevice_enable(NRF_CLOCK_LFCLKSRC_RC_250_PPM_TEMP_1000MS_CALIBRATION,
                                        sd_assert_handler));
    static ble_enable_params_t enable_params = {
        .gatts_enable_params = {
            .service_changed = 0,
            .attr_tab_size = 0,       /* 0 = SoftDevice default (0x700) */
        },
    };
    check("ble_en", sd_ble_enable(&enable_params));
    check("constlat", sd_power_mode_set(NRF_POWER_MODE_CONSTLAT));

    static const ble_gap_conn_sec_mode_t sec_mode = { 0 };  /* open, no SM */
    (void)sec_mode;
    apply_gap_config();        /* device name + optional address override */


    gatt_init();
    start_advertising();
    hwuart_puts("BLE advertising: ");
    hwuart_puts(g_cfg->name);
    hwuart_puts(" (svc FFF0 img / FFF2 cfg)\n");
}


/* ---------- main ---------- */
int main(void) {
    g_cfg = cfg_init();                   /* flash config or defaults */
    clock_start_hfclk();
    lfclk_start_rc();                     /* LFCLK required by RTC1     */
    rtc_init();                           /* RTC1: ms time base         */
    led_init();
    wake_input_init();
    uart_init();
    hwuart_puts("\r\nEINK-N51802 boot (WFT0420CZ15 400x300)\r\n");
    hwuart_puts(" CFG ");
    hwuart_puts(cfg_from_flash() ? "flash" : "defaults");
    hwuart_puts(" name=");
    hwuart_puts(g_cfg->name);
    hwuart_puts(" adv=");
    hwuart_put_u32(g_cfg->adv_timeout_s);
    hwuart_puts(" idle=");
    hwuart_put_u32(g_cfg->idle_rounds);
    hwuart_puts(" wake=");
    hwuart_put_u32(g_cfg->wake_enable);
    hwuart_puts(" addr=");
    hwuart_put_u32(g_cfg->addr_set);
    hwuart_puts("\r\n");
    hwuart_puts(" RESETREAS=0x");
    hwuart_put_hex32(REG(POWER_BASE, POWER_RESETREAS));
    hwuart_puts("\r\n");

    led_set(PIN_LED1, 1);                 /* P3 on: power/boot  */
    led_set(PIN_LED3, 1);                 /* P5 on: e-ink busy  */

    eink_gpio_init();
    /* Panel confirmed on hardware: this WFT0420CZ15 is the UC8176 ("old")
     * batch - the pattern only appeared when the stock-firmware-derived
     * UC8176 sequence ran. Waveshare's 0x2F read-back auto-detect needs
     * MISO, which this board does not route, so the variant is fixed
     * here. Set EINK_V2 instead to try the UC8179 batch. */
    eink_set_variant(EINK_V1);
    if (g_cfg->boot_refresh) {
        hwuart_puts("E-ink: UC8176 init\r\n");
        m_panel_busy = 1;
        eink_show_test_pattern();         /* blocking, full refresh     */
        m_panel_busy = 0;
        hwuart_puts("  status=0x");
        hwuart_put_hex32(eink_last_status);
        hwuart_puts(" busy_low=");
        hwuart_put_u32(eink_busy_low_samples);
        hwuart_puts(" first_ms=");
        hwuart_put_u32(eink_busy_first_ms);
        hwuart_puts(" release_ms=");
        hwuart_put_u32(eink_busy_release_ms);
        hwuart_puts(" waited_ms=");
        hwuart_put_u32(eink_refresh_wait_ms);
        hwuart_puts("\r\n");
    } else {
        /* Keep whatever the panel already shows (e-ink is bistable). */
        hwuart_puts("E-ink: boot refresh disabled\r\n");
    }
    eink_sleep();
    led_set(PIN_LED3, 0);                 /* refresh finished   */
    hwuart_puts("E-ink test pattern done\r\n");

    ble_init();
    p9_irq_init();                        /* P9 field -> GPIOTE PORT */
    led_set(PIN_LED1, 0);                 /* P3 off: idle       */

    static uint8_t evt_buf[128] __attribute__((aligned(4)));
    for (;;) {
        /* While the panel is refreshing, poll instead of sleeping in
         * sd_app_evt_wait() so BLE events keep being served. */
        led_tick();
        if (m_img_state == 3) {
            if ((int32_t)(rtc_ms() - m_refresh_until) >= 0) {
                eink_refresh_finish();
                m_img_state = 0;
                m_img_count = 0;
                led_set(PIN_LED3, 0);
                hwuart_puts("IMG done\r\n");
                start_advertising();   /* why: refresh ends the link */
            } else {
                delay_ms(10);
            }
        } else {
            /* Poll instead of sd_app_evt_wait() so the LED animation keeps
             * running; the radio still wakes us every advertising event. */
            delay_ms(20);
        }
        for (;;) {
            uint16_t len = (uint16_t)sizeof(evt_buf);
            uint32_t err = sd_ble_evt_get(evt_buf, &len);
            if (err != NRF_SUCCESS) break;
            uint16_t evt_id = (uint16_t)(evt_buf[0] | (evt_buf[1] << 8));
            if (evt_id == BLE_GAP_EVT_TIMEOUT) {
                m_idle_advertising_rounds++;
#if ENABLE_P9_WAKE
                /* Six silent 5 s advertising periods are enough; after that
                 * use the RF-coil P9 input as the hardware wake source. */
                if (g_cfg->wake_enable &&
                    m_idle_advertising_rounds >= g_cfg->idle_rounds) {
                    enter_system_off_for_wake();
                }
#endif
                start_advertising();
            } else if (evt_id == BLE_GAP_EVT_CONNECTED) {
                m_idle_advertising_rounds = 0;
                m_conn_handle = ((ble_evt_t *)evt_buf)->evt.gap_evt.conn_handle;
                led_set(PIN_LED2, 1);     /* LED2 on = BLE connected */
                hwuart_puts("BLE connected\r\n");
            } else if (evt_id == BLE_GAP_EVT_DISCONNECTED) {
                hwuart_puts("BLE disconnected\r\n");
                m_conn_handle = BLE_CONN_HANDLE_INVALID;
                if (m_img_state != 3) m_img_state = 0;   /* keep refreshing */
                m_img_count = 0;
                if (m_img_state != 3) start_advertising();
            } else if (evt_id == BLE_GATTS_EVT_WRITE) {
                ble_evt_t *pev = (ble_evt_t *)evt_buf;
                ble_gatts_evt_write_t *wr = &pev->evt.gatts_evt.params.write;
                if (wr->handle == m_img_handle) {
                    img_write(wr->data, wr->len);
                } else if (wr->handle == m_cfg_handle) {
                    cfg_write(wr->data, wr->len);
                }
            }
        }
        { uint32_t soc_evt; (void)sd_evt_get(&soc_evt); }     /* drain SoC events */

        /* P9 reacted to an RF field: keep the tag discoverable and give
         * visual feedback. Restarting advertising also resets the idle
         * countdown, so the tag stays awake while a phone is nearby. */
        if (m_p9_events) {
            m_p9_events = 0;
            int connected = (m_conn_handle != BLE_CONN_HANDLE_INVALID);
            hwuart_puts("P9 field detected\r\n");
            m_idle_advertising_rounds = 0;
            if (!connected) start_advertising();
            /* ~1.2 s of blinking so a manual test is unmistakable. */
            for (int i = 0; i < 6; i++) {
                led_set(PIN_LED2, 1);
                delay_ms(100);
                led_set(PIN_LED2, 0);
                delay_ms(100);
            }
            led_set(PIN_LED2, connected);     /* restore indication */
        }
    }
    return 0;
}