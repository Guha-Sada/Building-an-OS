/**
 * @file    stm32c031c6.h
 * @brief   Bare-metal peripheral register definitions for STM32C031C6
 *
 * Only the peripherals needed by this RTOS project are defined here.
 * Addresses taken from the STM32C0 reference manual (RM0490).
 *
 * Memory map:
 *   Flash  : 0x08000000  (32KB)
 *   SRAM   : 0x20000000  (12KB)
 *   APB    : 0x40000000
 *   AHB    : 0x40020000
 *   IOP    : 0x50000000  (GPIO)
 *   PPB    : 0xE0000000  (ARM core peripherals — SysTick, NVIC, SCB)
 */

#ifndef STM32C031C6_H
#define STM32C031C6_H

#include <stdint.h>

/* =========================================================================
 * Flash interface
 * ========================================================================= */
#define FLASH_BASE      0x40022000UL

typedef struct {
    volatile uint32_t ACR;          /*!< 0x00 Access control register       */
    volatile uint32_t RESERVED0;
    volatile uint32_t KEYR;         /*!< 0x08 Key register                  */
    volatile uint32_t OPTKEYR;      /*!< 0x0C Option key register           */
    volatile uint32_t SR;           /*!< 0x10 Status register               */
    volatile uint32_t CR;           /*!< 0x14 Control register              */
    volatile uint32_t ECCR;         /*!< 0x18 ECC register                  */
    volatile uint32_t RESERVED1;
    volatile uint32_t OPTR;         /*!< 0x20 Option register               */
} FLASH_TypeDef;

#define FLASH   ((FLASH_TypeDef *)FLASH_BASE)

/* FLASH ACR bits */
#define FLASH_ACR_LATENCY_Pos   0U
#define FLASH_ACR_LATENCY_Msk   (0x7UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_LATENCY_0WS   (0x0UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_LATENCY_1WS   (0x1UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_PRFTEN_Pos    8U
#define FLASH_ACR_PRFTEN_Msk    (0x1UL << FLASH_ACR_PRFTEN_Pos)

/* =========================================================================
 * Reset and Clock Control (RCC)
 * ========================================================================= */
#define RCC_BASE        0x40021000UL

typedef struct {
    volatile uint32_t CR;           /*!< 0x00 Clock control register        */
    volatile uint32_t ICSCR;        /*!< 0x04 Internal clock sources cal.   */
    volatile uint32_t CFGR;         /*!< 0x08 Clock configuration register  */
    volatile uint32_t RESERVED0;    /*!< 0x0C Reserved                      */
    volatile uint32_t CRRCR;        /*!< 0x10 Clock recovery RC register    */
    volatile uint32_t RESERVED1;    /*!< 0x14 Reserved                      */
    volatile uint32_t CIER;         /*!< 0x18 Clock interrupt enable        */
    volatile uint32_t CIFR;         /*!< 0x1C Clock interrupt flag          */
    volatile uint32_t CICR;         /*!< 0x20 Clock interrupt clear         */
    volatile uint32_t IOPRSTR;      /*!< 0x24 IO port reset register        */
    volatile uint32_t AHBRSTR;      /*!< 0x28 AHB peripheral reset          */
    volatile uint32_t APBRSTR1;     /*!< 0x2C APB peripheral reset 1        */
    volatile uint32_t APBRSTR2;     /*!< 0x30 APB peripheral reset 2        */
    volatile uint32_t IOPENR;       /*!< 0x34 IO port clock enable          */
    volatile uint32_t AHBENR;       /*!< 0x38 AHB peripheral clock enable   */
    volatile uint32_t APBENR1;      /*!< 0x3C APB peripheral clock enable 1 */
    volatile uint32_t APBENR2;      /*!< 0x40 APB peripheral clock enable 2 */
    volatile uint32_t IOPSMENR;     /*!< 0x44 IO port clock enable (sleep)  */
    volatile uint32_t AHBSMENR;     /*!< 0x48 AHB peripheral clk (sleep)    */
    volatile uint32_t APBSMENR1;    /*!< 0x4C APB peripheral clk 1 (sleep)  */
    volatile uint32_t APBSMENR2;    /*!< 0x50 APB peripheral clk 2 (sleep)  */
    volatile uint32_t CCIPR;        /*!< 0x54 Peripherals clock selection   */
    volatile uint32_t CCIPR2;       /*!< 0x58 Peripherals clock selection 2 */
    volatile uint32_t CSR;          /*!< 0x5C Control/status register       */
} RCC_TypeDef;

#define RCC     ((RCC_TypeDef *)RCC_BASE)

/* RCC CR bits */
#define RCC_CR_HSION_Pos        8U
#define RCC_CR_HSION_Msk        (0x1UL << RCC_CR_HSION_Pos)
#define RCC_CR_HSIRDY_Pos       10U
#define RCC_CR_HSIRDY_Msk       (0x1UL << RCC_CR_HSIRDY_Pos)
/*
 * HSIDIV[2:0] at bits [13:11]
 *   000 = HSI / 1  = 48 MHz  (target)
 *   001 = HSI / 2  = 24 MHz
 *   010 = HSI / 4  = 12 MHz  (reset default)
 *   011 = HSI / 8  =  6 MHz
 *   100 = HSI / 16 =  3 MHz
 *   101 = HSI / 32 = 1.5 MHz
 *   110 = HSI / 64 = 0.75 MHz
 *   111 = HSI / 128 = 0.375 MHz
 */
#define RCC_CR_HSIDIV_Pos       11U
#define RCC_CR_HSIDIV_Msk       (0x7UL << RCC_CR_HSIDIV_Pos)
#define RCC_CR_HSIDIV_1         (0x0UL << RCC_CR_HSIDIV_Pos)   /*!< 48 MHz */
#define RCC_CR_HSIDIV_4         (0x2UL << RCC_CR_HSIDIV_Pos)   /*!< 12 MHz (reset) */

/* RCC CFGR bits — SW/SWS clock source selection */
#define RCC_CFGR_SW_Pos         0U
#define RCC_CFGR_SW_Msk         (0x7UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSI         (0x0UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SWS_Pos        3U
#define RCC_CFGR_SWS_Msk        (0x7UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSI        (0x0UL << RCC_CFGR_SWS_Pos)

/* RCC IOPENR bits — GPIO clock gates */
#define RCC_IOPENR_GPIOAEN_Pos  0U
#define RCC_IOPENR_GPIOAEN_Msk  (0x1UL << RCC_IOPENR_GPIOAEN_Pos)
#define RCC_IOPENR_GPIOBEN_Pos  1U
#define RCC_IOPENR_GPIOBEN_Msk  (0x1UL << RCC_IOPENR_GPIOBEN_Pos)
#define RCC_IOPENR_GPIOCEN_Pos  2U
#define RCC_IOPENR_GPIOCEN_Msk  (0x1UL << RCC_IOPENR_GPIOCEN_Pos)

/* =========================================================================
 * GPIO — base addresses
 * ========================================================================= */
#define GPIOA_BASE      0x50000000UL
#define GPIOB_BASE      0x50000400UL
#define GPIOC_BASE      0x50000800UL

typedef struct {
    volatile uint32_t MODER;        /*!< 0x00 Mode register                 */
    volatile uint32_t OTYPER;       /*!< 0x04 Output type register          */
    volatile uint32_t OSPEEDR;      /*!< 0x08 Output speed register         */
    volatile uint32_t PUPDR;        /*!< 0x0C Pull-up/pull-down register    */
    volatile uint32_t IDR;          /*!< 0x10 Input data register           */
    volatile uint32_t ODR;          /*!< 0x14 Output data register          */
    volatile uint32_t BSRR;         /*!< 0x18 Bit set/reset register        */
    volatile uint32_t LCKR;         /*!< 0x1C Configuration lock register   */
    volatile uint32_t AFR[2];       /*!< 0x20 Alternate function registers  */
    volatile uint32_t BRR;          /*!< 0x28 Bit reset register            */
} GPIO_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB   ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC   ((GPIO_TypeDef *)GPIOC_BASE)

/* GPIO MODER field values (2 bits per pin) */
#define GPIO_MODER_INPUT        0x0U
#define GPIO_MODER_OUTPUT       0x1U
#define GPIO_MODER_AF           0x2U
#define GPIO_MODER_ANALOG       0x3U

/* Helper macros for GPIO pin operations */
#define GPIO_SET(port, pin)     ((port)->BSRR = (1U << (pin)))
#define GPIO_CLR(port, pin)     ((port)->BSRR = (1U << ((pin) + 16)))
#define GPIO_TOG(port, pin)     ((port)->ODR ^= (1U << (pin)))

/* Nucleo-C031C6 user LED: PA5 */
#define LED_PORT    GPIOA
#define LED_PIN     5U

/* =========================================================================
 * ARM Cortex-M0+ Core Peripherals (PPB — Private Peripheral Bus)
 * These are defined by ARM, not ST, so they are common across all M0+ parts.
 * ========================================================================= */

/* --- SysTick --- */
#define SYSTICK_BASE    0xE000E010UL

typedef struct {
    volatile uint32_t CTRL;         /*!< 0x00 Control and status            */
    volatile uint32_t LOAD;         /*!< 0x04 Reload value                  */
    volatile uint32_t VAL;          /*!< 0x08 Current value                 */
    volatile uint32_t CALIB;        /*!< 0x0C Calibration                   */
} SysTick_TypeDef;

#define SYSTICK     ((SysTick_TypeDef *)SYSTICK_BASE)

#define SYSTICK_CTRL_ENABLE_Pos     0U
#define SYSTICK_CTRL_ENABLE_Msk     (0x1UL << SYSTICK_CTRL_ENABLE_Pos)
#define SYSTICK_CTRL_TICKINT_Pos    1U
#define SYSTICK_CTRL_TICKINT_Msk    (0x1UL << SYSTICK_CTRL_TICKINT_Pos)
#define SYSTICK_CTRL_CLKSOURCE_Pos  2U
#define SYSTICK_CTRL_CLKSOURCE_Msk  (0x1UL << SYSTICK_CTRL_CLKSOURCE_Pos)
#define SYSTICK_CTRL_COUNTFLAG_Pos  16U
#define SYSTICK_CTRL_COUNTFLAG_Msk  (0x1UL << SYSTICK_CTRL_COUNTFLAG_Pos)

/* --- System Control Block (SCB) --- */
#define SCB_BASE        0xE000ED00UL

typedef struct {
    volatile uint32_t CPUID;        /*!< 0x00 CPUID base register           */
    volatile uint32_t ICSR;         /*!< 0x04 Interrupt control/state       */
    volatile uint32_t RESERVED0;    /*!< 0x08 (no VTOR on M0+, or optional) */
    volatile uint32_t AIRCR;        /*!< 0x0C App interrupt/reset control   */
    volatile uint32_t SCR;          /*!< 0x10 System control register       */
    volatile uint32_t CCR;          /*!< 0x14 Configuration and control     */
    volatile uint32_t RESERVED1;    /*!< 0x18 (SHPR1 not on M0+)           */
    volatile uint32_t SHPR2;        /*!< 0x1C System handler priority 2     */
    volatile uint32_t SHPR3;        /*!< 0x20 System handler priority 3     */
} SCB_TypeDef;

#define SCB     ((SCB_TypeDef *)SCB_BASE)

/* SCB ICSR bits */
#define SCB_ICSR_PENDSVSET_Pos  28U
#define SCB_ICSR_PENDSVSET_Msk  (0x1UL << SCB_ICSR_PENDSVSET_Pos)
#define SCB_ICSR_PENDSVCLR_Pos  27U
#define SCB_ICSR_PENDSVCLR_Msk  (0x1UL << SCB_ICSR_PENDSVCLR_Pos)

/*
 * System handler priorities on Cortex-M0+
 * SHPR2: bits [31:24] = SVC priority
 * SHPR3: bits [31:24] = SysTick priority, bits [23:16] = PendSV priority
 *
 * Only 2 priority bits are implemented (bits [7:6] of each 8-bit field).
 * Encoded values: 0x00 = highest, 0x40, 0x80, 0xC0 = lowest.
 */
#define SCB_SHPR3_PENDSV_PRI_LOWEST     (0xC0UL << 16U)  /*!< PendSV = lowest  */
#define SCB_SHPR3_SYSTICK_PRI_HIGH      (0x40UL << 24U)  /*!< SysTick = high   */

/* --- NVIC (minimal — only what we need) --- */
#define NVIC_BASE       0xE000E100UL

/* =========================================================================
 * CONTROL register helpers (accessed via MSR/MRS in assembly, or intrinsics)
 * ========================================================================= */
#define CONTROL_SPSEL_Msk   (0x1UL << 1)   /*!< Use PSP in thread mode      */
#define CONTROL_nPRIV_Msk   (0x1UL << 0)   /*!< Unprivileged thread mode    */

/* =========================================================================
 * Compiler / instruction helpers
 * ========================================================================= */
#define __NOP()         __asm volatile ("nop")
#define __WFI()         __asm volatile ("wfi")
#define __DSB()         __asm volatile ("dsb 0xF" ::: "memory")
#define __ISB()         __asm volatile ("isb 0xF" ::: "memory")
#define __CPSID_I()     __asm volatile ("cpsid i" ::: "memory")
#define __CPSIE_I()     __asm volatile ("cpsie i" ::: "memory")

static inline uint32_t __get_PSP(void) {
    uint32_t result;
    __asm volatile ("mrs %0, psp" : "=r" (result));
    return result;
}

static inline void __set_PSP(uint32_t val) {
    __asm volatile ("msr psp, %0" :: "r" (val) : "memory");
}

static inline uint32_t __get_CONTROL(void) {
    uint32_t result;
    __asm volatile ("mrs %0, control" : "=r" (result));
    return result;
}

static inline void __set_CONTROL(uint32_t val) {
    __asm volatile ("msr control, %0" :: "r" (val) : "memory");
    __ISB();    /* Required after writing CONTROL */
}

/* Trigger PendSV (safe to call from both thread and handler mode) */
static inline void trigger_pendsv(void) {
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    __DSB();
    __ISB();
}

/* SystemCoreClock — updated by SystemClock_Config() */
extern uint32_t SystemCoreClock;

#endif /* STM32C031C6_H */
