/* Minimal CMSIS stand-in so the S110 headers (ble*.h, nrf_s*.h) compile
 * without pulling in the whole nRF5 SDK. The headers only need integer
 * types from nrf.h; all register definitions we use come from nrf51_min.h.
 */
#ifndef NRF_H__
#define NRF_H__
#include <stdint.h>
#include <stdbool.h>

/* CMSIS interrupt-number type (normally from core_cm0.h via nrf51.h). */
typedef int32_t IRQn_Type;
#endif
