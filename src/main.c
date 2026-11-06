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
#define ENABLE_P9_WAKE 0
#endif

static uint32_t m_idle_advertising_rounds;

static void wake_input_init(void) {
    /* Harmless with the wake path disabled: SENSE only drives the DETECT
     * signal, and no GPIOTE/port interrupt is enabled. */
    gpio_dir_input(PIN_WAKE, CFG_INPUT_SENSE_HIGH);
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
#define ADV_TIMEOUT_S      5        /* re-arm advertising every 5 s */

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
    static const uint8_t scan_rsp[] = {
        0x0C, 0x09, 'E', 'I', 'N', 'K', '-', 'N', '5', '1', '8', '0', '2',
    };
    static const ble_gap_adv_params_t adv_params = {
        .type          = BLE_GAP_ADV_TYPE_ADV_IND,
        .p_peer_addr   = 0,
        .fp            = BLE_GAP_ADV_FP_ANY,
        .p_whitelist   = 0,
        .interval      = ADV_INTERVAL_UNITS,
        .timeout       = ADV_TIMEOUT_S,
        .channel_mask  = { 0, 0, 0 },
    };
    uint32_t err = sd_ble_gap_adv_data_set(adv_payload, (uint8_t)sizeof(adv_payload),
                                           scan_rsp, (uint8_t)sizeof(scan_rsp));
    check("adv_data", err);
    err = sd_ble_gap_adv_start(&adv_params);
    check("adv_start", err);
    led_set(PIN_LED2, 1);
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

static uint16_t m_img_handle;
static uint32_t m_img_state;      /* 0 idle, 1 black plane, 2 red plane, 3 refreshing */
static uint32_t m_img_count;      /* bytes pushed into the current plane  */
static uint32_t m_refresh_until;  /* rtc_ms() deadline while refreshing   */
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;

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
    m_img_state = 3;
    m_refresh_until = rtc_ms() + IMG_REFRESH_MS;
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
    check("name", sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)"EINK-N51802", 11));


    gatt_init();
    start_advertising();
    hwuart_puts("BLE advertising: EINK-N51802 (svc FFF0, write FFF1)\n");
}


/* ---------- main ---------- */
int main(void) {
    clock_start_hfclk();
    lfclk_start_rc();                     /* LFCLK required by RTC1     */
    rtc_init();                           /* RTC1: ms time base         */
    led_init();
    wake_input_init();
    uart_init();
    hwuart_puts("\r\nEINK-N51802 boot (WFT0420CZ15 400x300)\r\n");
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
    hwuart_puts("E-ink: UC8176 init\r\n");
    eink_show_test_pattern();             /* blocking, full refresh     */
    hwuart_puts("  status=0x");
    hwuart_put_hex32(eink_last_status);
    hwuart_puts(" waited_ms=");
    hwuart_put_u32(eink_refresh_wait_ms);
    hwuart_puts("\r\n");
    eink_sleep();
    led_set(PIN_LED3, 0);                 /* refresh finished   */
    hwuart_puts("E-ink test pattern done\r\n");

    ble_init();
    led_set(PIN_LED1, 0);                 /* P3 off: idle       */

    static uint8_t evt_buf[128] __attribute__((aligned(4)));
    for (;;) {
        /* While the panel is refreshing, poll instead of sleeping in
         * sd_app_evt_wait() so BLE events keep being served. */
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
            sd_app_evt_wait();
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
                if (m_idle_advertising_rounds >= 6) {
                    enter_system_off_for_wake();
                }
#endif
                start_advertising();
            } else if (evt_id == BLE_GAP_EVT_CONNECTED) {
                m_idle_advertising_rounds = 0;
                m_conn_handle = ((ble_evt_t *)evt_buf)->evt.gap_evt.conn_handle;
                led_set(PIN_LED2, 0);     /* solid while connected */
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
                }
            }
        }
        { uint32_t soc_evt; (void)sd_evt_get(&soc_evt); }     /* drain SoC events */
    }
    return 0;
}