/* Cortex-M0 startup + vector table for the APPLICATION at 0x18000
 * (right after the S110 SoftDevice, which occupies 0x00000-0x17FFF).
 *
 * The SoftDevice redirects the core to this Reset handler on boot, and
 * forwards application IRQs here once the app calls
 * sd_softdevice_vector_table_base_set(0x18000). VTOR itself stays 0 (the
 * SoftDevice's own table) - its entry for an application IRQ jumps to
 * app_base + the *standard CMSIS* vector offset, so the layout below MUST
 * follow ARMv6-M exception numbering:
 *
 *   0  SP            8  reserved      16 IRQ0
 *   1  Reset         9  reserved      17 IRQ1
 *   2  NMI          10  reserved      ...
 *   3  HardFault    11  SVCall        22 IRQ6  <- GPIOTE
 *   4  reserved     12  DebugMon      47 IRQ31
 *   5  reserved     13  reserved
 *   6  reserved     14  PendSV
 *   7  reserved     15  SysTick
 *
 * A compact table (SVC/PendSV/SysTick squeezed into 4..6, IRQs from 7)
 * silently misroutes every IRQ: the SD forwards IRQ6 to offset 0x58,
 * which then held unrelated code, so the GPIOTE handler never ran.
 */
#include <stdint.h>

extern uint32_t __etext;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);
/* IRQ 6 = GPIOTE: used for the P9 (RF coil) PORT event. */
void GPIOTE_IRQHandler(void);

__attribute__((used, section(".vector_table")))
uint32_t __Vectors[48] = {
    0x20004000UL,                  /* 0  SP: end of app RAM              */
    (uint32_t)Reset_Handler,       /* 1  Reset                           */
    (uint32_t)Default_Handler,     /* 2  NMI                             */
    (uint32_t)Default_Handler,     /* 3  HardFault                       */
    (uint32_t)Default_Handler,     /* 4  reserved                        */
    (uint32_t)Default_Handler,     /* 5  reserved                        */
    (uint32_t)Default_Handler,     /* 6  reserved                        */
    (uint32_t)Default_Handler,     /* 7  reserved                        */
    (uint32_t)Default_Handler,     /* 8  reserved                        */
    (uint32_t)Default_Handler,     /* 9  reserved                        */
    (uint32_t)Default_Handler,     /* 10 reserved                        */
    (uint32_t)Default_Handler,     /* 11 SVCall (handled by SoftDevice)  */
    (uint32_t)Default_Handler,     /* 12 DebugMon                        */
    (uint32_t)Default_Handler,     /* 13 reserved                        */
    (uint32_t)Default_Handler,     /* 14 PendSV                          */
    (uint32_t)Default_Handler,     /* 15 SysTick                         */
    /* IRQ 0..31 */
    (uint32_t)Default_Handler,     /* 16 IRQ0  POWER_CLOCK               */
    (uint32_t)Default_Handler,     /* 17 IRQ1  RADIO                     */
    (uint32_t)Default_Handler,     /* 18 IRQ2  UART0                     */
    (uint32_t)Default_Handler,     /* 19 IRQ3  SPI0_TWI0                 */
    (uint32_t)Default_Handler,     /* 20 IRQ4  SPI1_TWI1                 */
    (uint32_t)Default_Handler,     /* 21 IRQ5  (reserved / NFCT)         */
    (uint32_t)GPIOTE_IRQHandler,   /* 22 IRQ6  GPIOTE  <- P9 port event  */
    (uint32_t)Default_Handler,     /* 23 IRQ7  ADC                       */
    (uint32_t)Default_Handler,     /* 24 IRQ8  TIMER0                    */
    (uint32_t)Default_Handler,     /* 25 IRQ9  TIMER1                    */
    (uint32_t)Default_Handler,     /* 26 IRQ10 TIMER2                    */
    (uint32_t)Default_Handler,     /* 27 IRQ11 RTC0                      */
    (uint32_t)Default_Handler,     /* 28 IRQ12 TEMP                      */
    (uint32_t)Default_Handler,     /* 29 IRQ13 RNG                       */
    (uint32_t)Default_Handler,     /* 30 IRQ14 ECB                       */
    (uint32_t)Default_Handler,     /* 31 IRQ15 CCM                       */
    (uint32_t)Default_Handler,     /* 32 IRQ16 AAR                       */
    (uint32_t)Default_Handler,     /* 33 IRQ17 WDT                       */
    (uint32_t)Default_Handler,     /* 34 IRQ18 RTC1                      */
    (uint32_t)Default_Handler,     /* 35 IRQ19 QDEC                      */
    (uint32_t)Default_Handler,     /* 36 IRQ20 LPCOMP                    */
    (uint32_t)Default_Handler,     /* 37 IRQ21 SWI0                      */
    (uint32_t)Default_Handler,     /* 38 IRQ22 SWI1                      */
    (uint32_t)Default_Handler,     /* 39 IRQ23 SWI2                      */
    (uint32_t)Default_Handler,     /* 40 IRQ24 SWI3                      */
    (uint32_t)Default_Handler,     /* 41 IRQ25 SWI4                      */
    (uint32_t)Default_Handler,     /* 42 IRQ26 SWI5                      */
    (uint32_t)Default_Handler,     /* 43 IRQ27 (reserved)                */
    (uint32_t)Default_Handler,     /* 44 IRQ28 (reserved)                */
    (uint32_t)Default_Handler,     /* 45 IRQ29 (reserved)                */
    (uint32_t)Default_Handler,     /* 46 IRQ30 (reserved)                */
    (uint32_t)Default_Handler,     /* 47 IRQ31 (reserved)                */
};

/* Symbols provided by the linker script */
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;

void Reset_Handler(void) {
    /* copy .data from flash to RAM */
    uint32_t *src = &__etext;
    uint32_t *dst = &__data_start__;
    while (dst < &__data_end__) *dst++ = *src++;
    /* zero .bss */
    dst = &__bss_start__;
    while (dst < &__bss_end__) *dst++ = 0;
    main();
    while (1) { }
}

void Default_Handler(void) {
    while (1) { }
}
