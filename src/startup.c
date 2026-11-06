/* Cortex-M0 startup + vector table for the APPLICATION at 0x18000
 * (right after the S110 SoftDevice, which occupies 0x00000-0x17FFF).
 *
 * The SoftDevice redirects the core to this Reset handler on boot, and
 * IRQ/SVC traffic is forwarded here once the app calls
 * sd_softdevice_vector_table_base_set(0x18000).
 *
 * All interrupt vectors except Reset point at a default handler that
 * loops forever; the app itself needs no interrupts beyond what the
 * SoftDevice manages (no SysTick/RTC/TIMER usage).
 */
#include <stdint.h>

extern uint32_t __etext;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

__attribute__((used, section(".vector_table")))
uint32_t __Vectors[48] = {
    0x20004000UL, /* SP end of app RAM (0x20002000+0x2000) */
    (uint32_t)Reset_Handler,
    (uint32_t)Default_Handler, /* NMI      */
    (uint32_t)Default_Handler, /* HardFault*/
    (uint32_t)Default_Handler, /* SVC      */
    (uint32_t)Default_Handler, /* PendSV   */
    (uint32_t)Default_Handler, /* SysTick  */
    /* IRQs 0..31 */
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
    (uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,(uint32_t)Default_Handler,
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
