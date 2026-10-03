// Firmware de diagnostic MKS Robin Nano : aucune dépendance (ni HAL, ni startup, ni RAM initialisée, ni libc).
// Sortie texte sur USART3 (PB10 TX -> CH340 -> /dev/ttyUSB0), 115200 8N1, répétée chaque seconde.
//   "RESET ..."  -> le bootloader a lancé ce code ; D7 fixe allumée entre deux messages
//   "FAULT ..."  -> une exception a été prise : PC fautif, LR, CFSR, HFSR... + dump de la flash
// L'horloge est ramenée sur HSI 8 MHz (le bootloader laisse la PLL active), donc le débit est connu.
#include <stdint.h>

#define REG(a)        (*(volatile uint32_t *)(a))
#define RCC_CR        REG(0x40021000)
#define RCC_CFGR      REG(0x40021004)
#define RCC_APB1RSTR  REG(0x40021010)
#define RCC_APB2ENR   REG(0x40021018)
#define RCC_APB1ENR   REG(0x4002101C)
#define GPIOB_CRH     REG(0x40010C04)
#define GPIOB_BRR     REG(0x40010C14)
#define USART3_SR     REG(0x40004800)
#define USART3_DR     REG(0x40004804)
#define USART3_BRR    REG(0x40004808)
#define USART3_CR1    REG(0x4000480C)
#define SCB_VTOR      REG(0xE000ED08)
#define SCB_SHCSR     REG(0xE000ED24)
#define SCB_CFSR      REG(0xE000ED28)
#define SCB_HFSR      REG(0xE000ED2C)
#define SCB_MMFAR     REG(0xE000ED34)
#define SCB_BFAR      REG(0xE000ED38)

extern uint32_t _estack;

static void wait(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }

static void clock_hsi(void)
{
    RCC_CR |= 1u;                       // HSION
    while (!(RCC_CR & 2u)) { }          // HSIRDY
    RCC_CFGR = 0;                       // SYSCLK = HSI, tous les diviseurs à 1
    while (RCC_CFGR & 0xCu) { }         // SWS = HSI
}

static void led_on(void)
{
    RCC_APB2ENR |= 1u << 3;                               // IOPBEN
    GPIOB_CRH = (GPIOB_CRH & ~(0xFu << 8)) | (0x2u << 8); // PB10 sortie push-pull
    GPIOB_BRR = 1u << 10;                                 // PB10 = 0 -> D7 allumée
}

static void uart_init(void)
{
    clock_hsi();
    RCC_APB2ENR |= 1u << 3;                               // IOPBEN
    RCC_APB1ENR |= 1u << 18;                              // USART3EN
    RCC_APB1RSTR |= 1u << 18; RCC_APB1RSTR &= ~(1u << 18);
    GPIOB_CRH = (GPIOB_CRH & ~(0xFu << 8)) | (0xAu << 8); // PB10 AF push-pull
    USART3_BRR = 69;                                      // 8 MHz / 115200
    USART3_CR1 = (1u << 13) | (1u << 3);                  // UE | TE
}

static void putc_(char c) { while (!(USART3_SR & 0x80u)) { } USART3_DR = (uint32_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) putc_("0123456789ABCDEF"[(v >> i) & 15]);
}
static void field(const char *name, uint32_t v) { puts_(name); putc_('='); hex(v); putc_(' '); }
static void dump(uint32_t addr, int words)
{
    hex(addr); puts_(":");
    for (int i = 0; i < words; i++) { putc_(' '); hex(*(volatile uint32_t *)(addr + 4u * i)); }
    puts_("\r\n");
}
static void flush(void) { while (!(USART3_SR & 0x40u)) { } }   // TC

void Reset_Handler(void)
{
    __asm volatile("cpsid i");
    led_on();                                             // D7 fixe : le code est lancé
    for (;;) {
        uart_init();
        puts_("RESET ");
        field("VTOR", SCB_VTOR);
        field("SHCSR", SCB_SHCSR);
        field("CFSR", SCB_CFSR);
        field("HFSR", SCB_HFSR);
        puts_("\r\n");
        flush();
        led_on();
        wait(1300000);
    }
}

// Appelée par Fault_Handler avec le pointeur sur la trame d'exception empilée (r0-r3, r12, lr, pc, xpsr)
void __attribute__((noinline, used)) fault_c(uint32_t *frame, uint32_t excret)
{
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    for (;;) {
        uart_init();
        puts_("FAULT ");
        field("IPSR", ipsr);
        field("PC", frame[6]);
        field("LR", frame[5]);
        field("XPSR", frame[7]);
        field("EXCRET", excret);
        puts_("\r\n      ");
        field("R0", frame[0]); field("R1", frame[1]); field("R2", frame[2]); field("R3", frame[3]);
        field("R12", frame[4]); field("FRAME", (uint32_t)frame);
        puts_("\r\n      ");
        field("CFSR", SCB_CFSR); field("HFSR", SCB_HFSR);
        field("MMFAR", SCB_MMFAR); field("BFAR", SCB_BFAR); field("VTOR", SCB_VTOR);
        puts_("\r\n");
        puts_("FLASH "); dump(0x08007000, 8);
        puts_("CODE  "); dump(frame[6] & ~1u, 4);
        puts_("CODE  "); dump(0x08007190, 4);
        flush();
        led_on();
        wait(1300000);
    }
}

__attribute__((naked)) void Fault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b fault_c\n");
}

__attribute__((section(".isr_vector"), used))
const uint32_t g_vectors[76] = {
    [0]        = (uint32_t)&_estack,
    [1]        = (uint32_t)Reset_Handler,
    [2 ... 75] = (uint32_t)Fault_Handler,
};
