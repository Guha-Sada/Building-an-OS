/**
 * @file    clock.c
 * @brief   Bare-metal system clock and GPIO initialization for STM32C031C6
 *
 * Goal: bring the core up to 48 MHz using the internal HSI48 oscillator,
 * then enable the GPIOA clock and configure PA5 (Nucleo user LED) as output.
 *
 * Clock path on STM32C031C6:
 *   HSI48 (48 MHz) → HSIDIV (/4 by default) → SYSCLK (12 MHz at reset)
 *
 * To reach 48 MHz:
 *   1. Set Flash wait state to 1 (required for f_CPU > 24 MHz)
 *   2. Clear HSIDIV to 000 (divide-by-1)
 *   3. Wait for HSI to stabilise (HSIRDY)
 *   4. Confirm SYSCLK source = HSI (it is at reset; no switch needed)
 */

#include "stm32c031c6.h"

/* Updated by SystemClock_Config(); used by SysTick_Init() */
uint32_t SystemCoreClock = 12000000UL;  /* Conservative reset default */

/* -------------------------------------------------------------------------
 * SystemClock_Config
 *
 * Configure SYSCLK = HSI48 / 1 = 48 MHz (maximum for this device).
 * ------------------------------------------------------------------------- */
void SystemClock_Config(void)
{
    /* --- Step 1: increase Flash wait states BEFORE raising frequency ---
     *
     * The Flash read latency must be set first.  If we speed up the clock
     * and then set wait states we risk a bus fault on the very instruction
     * that writes the wait state.
     *
     * ACR.LATENCY = 1 → 1 wait state (required for 24 MHz < f ≤ 48 MHz)
     * ACR.PRFTEN  = 1 → enable prefetch buffer (improves throughput)
     */
    uint32_t acr = FLASH->ACR;
    acr &= ~FLASH_ACR_LATENCY_Msk;
    acr |=  FLASH_ACR_LATENCY_1WS;
    acr |=  FLASH_ACR_PRFTEN_Msk;
    FLASH->ACR = acr;

    /* Confirm the write took effect before touching the clock */
    while ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != FLASH_ACR_LATENCY_1WS) {}

    /* --- Step 2: remove the HSI divide-by-4 (set HSIDIV = 000 → /1) ---
     *
     * HSIDIV lives at bits [13:11] of RCC_CR.  Clearing those bits sets
     * the divider to /1, giving us HSI = 48 MHz.
     *
     * HSION (bit 8) is already set at reset; we leave it set.
     */
    uint32_t cr = RCC->CR;
    cr &= ~RCC_CR_HSIDIV_Msk;       /* Clear divider → /1  */
    RCC->CR = cr;

    /* --- Step 3: wait for the HSI to re-lock at the new frequency ---
     *
     * HSIRDY (bit 10) goes low briefly when we change the divider, then
     * rises again once the oscillator is stable.
     */
    while (!(RCC->CR & RCC_CR_HSIRDY_Msk)) {}

    /* --- Step 4: SYSCLK is already sourced from HSI (SWS = 000 = HSI) ---
     *
     * On this device there is no PLL, so no clock switch is needed.
     * We just confirm SWS still shows HSI.
     */
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_HSI) {}

    /* Update the global variable so SysTick_Init() knows the true frequency */
    SystemCoreClock = 48000000UL;
}

/* -------------------------------------------------------------------------
 * GPIO_Init
 *
 * Enable GPIOA clock and configure PA5 as push-pull output (Nucleo LED).
 * ------------------------------------------------------------------------- */
void GPIO_Init(void)
{
    /* Enable GPIOA peripheral clock via RCC_IOPENR */
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN_Msk;

    /* Short delay to let the clock gate open (at least 1 bus cycle) */
    (void)RCC->IOPENR;

    /* Configure PA5 as general-purpose output (MODER bits [11:10] = 01)
     *
     * MODER is reset to 0xEBFF_FFFF for GPIOA (most pins are analog).
     * We must clear both mode bits for pin 5 then set the output bit.
     *
     * Pin 5 → MODER field starts at bit (5 * 2) = 10.
     */
    uint32_t moder = GPIOA->MODER;
    moder &= ~(0x3U << (LED_PIN * 2));          /* Clear bits [11:10] */
    moder |=  (GPIO_MODER_OUTPUT << (LED_PIN * 2)); /* Set output mode  */
    GPIOA->MODER = moder;

    /* Output type: push-pull (OTYPER bit 5 = 0, which is already the reset
     * value, so no explicit write is needed — but we'll be explicit) */
    GPIOA->OTYPER &= ~(0x1U << LED_PIN);

    /* Output speed: low speed (OSPEEDR bits [11:10] = 00, already reset) */
    GPIOA->OSPEEDR &= ~(0x3U << (LED_PIN * 2));

    /* No pull-up/pull-down (PUPDR bits [11:10] = 00, already reset) */
    GPIOA->PUPDR &= ~(0x3U << (LED_PIN * 2));

    /* Start with LED off */
    GPIO_CLR(LED_PORT, LED_PIN);
}
