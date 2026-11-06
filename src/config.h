/* Persistent configuration in the last flash page.
 *
 * Storage: one 1 KB page at 0x3FC00 (nRF51 flash page size is 1 KB, and the
 * application image is only ~11 KB at 0x18000, so this page is far away
 * from anything the linker places). A power-on load validates magic + CRC
 * and falls back to compile-time defaults; a save erases the page and
 * rewrites the struct.
 *
 * With S110 enabled the application must NOT touch the NVMC directly - the
 * SoftDevice owns the flash controller - so writes go through
 * sd_flash_page_erase()/sd_flash_write(), retrying on NRF_ERROR_BUSY.
 * UICR is deliberately not used: on nRF51 its only erase path is ERASEALL,
 * which would wipe the whole chip.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define CFG_PAGE_ADDR   0x3FC00UL
#define CFG_PAGE_SIZE   1024UL
#define CFG_MAGIC       0x45494E4BUL        /* 'EINK' */
#define CFG_VERSION     5
#define CFG_NAME_LEN    16

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t adv_timeout_s;      /* GAP advertising timeout, seconds      */
    uint16_t idle_rounds;        /* silent adv rounds before sleeping     */
    uint8_t  wake_enable;        /* 1 = P9 SYSTEM OFF wake allowed        */
    uint8_t  boot_refresh;       /* 1 = draw the info screen at boot      */
    uint8_t  panel_sleep;        /* 1 = deep-sleep the panel after a refresh */
    uint8_t  flags;              /* reserved                              */
    char     name[CFG_NAME_LEN]; /* BLE device name, NUL padded           */
    uint8_t  addr[6];            /* BLE address, little-endian            */
    uint8_t  addr_set;           /* 0 = use the FICR factory address      */
    uint8_t  addr_pad[2];
    uint32_t crc;                /* over everything above                 */
} cfg_t;

/* Load from flash (or defaults). Safe to call once at boot. */
const cfg_t *cfg_init(void);

/* Current live configuration (RAM copy). */
cfg_t *cfg_live(void);

/* Persist the live configuration to flash. Returns 0 on success. */
uint32_t cfg_save(void);

/* Restore compile-time defaults into the live copy (not persisted). */
void cfg_defaults(void);

/* Where the live values came from, for logging: 1 = flash, 0 = defaults. */
int cfg_from_flash(void);

/* Parse one "key=value" text command into the live config.
 * Returns 1 if the config changed, 0 if unknown key, -1 on bad value.
 * Supported: name=<str>, adv=<sec>, idle=<rounds>, wake=<0|1>,
 *            addr=AA:BB:CC:DD:EE:FF, addr=auto (use FICR),
 *            boot=<0|1> (redraw the screen on reset),
 *            reset. */
int cfg_apply_text(const char *s, uint32_t len);

#endif
/* config.h */
