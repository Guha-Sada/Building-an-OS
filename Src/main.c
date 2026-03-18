/**
 * @file    main.c
 * @brief   RTOS demo — two tasks scheduled by the round-robin scheduler.
 *
 * Phase 1 : clock init, GPIO, SysTick, LED blink verification.  ✓
 * Phase 2 : sys_tick / SysTick_Handler moved to rtos.c.          ✓
 * Phase 3-4: RTOS starts here — two tasks, context switching live.
 *
 * HOW TO VERIFY PHASE 3-4
 * -----------------------
 * Flash and observe PA5 (Nucleo green LED):
 *
 *   task_led  : LED on  → osDelay(500) → LED off → osDelay(500)  → repeat
 *   task_count: increments a global counter every ~10 ms
 *
 * Expected observations:
 *   - LED blinks at ~1 Hz (500 ms on, 500 ms off).
 *     Proves task_led is being scheduled and context-switched correctly.
 *
 *   - task2_counter increments in the background while task_led is delaying.
 *     Probe it in the debugger (Live Expressions / Watch window) — it should
 *     increment ~100 times per second even while task_led is "sleeping".
 *     Proves the scheduler runs task2 during task_led's delay periods.
 *
 * NOTE ON osDelay() IN THIS PHASE
 * --------------------------------
 * osDelay() is still a spinwait (Phase 5 replaces it with a real sleep).
 * The tasks still get preempted every 1 ms by PendSV, so both run
 * concurrently in 1 ms round-robin slices.  The delay durations are correct
 * (they track wall-clock time via sys_tick) but CPU is not yielded — that
 * improvement comes in Phase 5.
 */

#include <stdint.h>
#include "stm32c031c6.h"
#include "scheduler.h"
#include "rtos.h"

/* -------------------------------------------------------------------------
 * Forward declarations (defined in clock.c)
 * ------------------------------------------------------------------------- */
void SystemClock_Config(void);
void GPIO_Init(void);

/* -------------------------------------------------------------------------
 * SysTick_Init
 *
 * 1 ms tick at 48 MHz. PendSV set to lowest priority so it only fires
 * after all higher-priority ISRs have returned.
 * ------------------------------------------------------------------------- */
static void SysTick_Init(void)
{
    SYSTICK->LOAD = (SystemCoreClock / 1000U) - 1U;    /* reload = 47 999   */
    SYSTICK->VAL  = 0U;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE_Msk
                  | SYSTICK_CTRL_TICKINT_Msk
                  | SYSTICK_CTRL_ENABLE_Msk;

    /* SHPR3 [31:24] = SysTick prio 0x40, [23:16] = PendSV prio 0xC0       */
    SCB->SHPR3 = SCB_SHPR3_PENDSV_PRI_LOWEST
               | SCB_SHPR3_SYSTICK_PRI_HIGH;
}

/* =========================================================================
 * Task stacks  (static, 256 bytes each = 64 words)
 * ========================================================================= */
static uint32_t task_led_stack  [TASK_STACK_WORDS_DEFAULT];
static uint32_t task_count_stack[TASK_STACK_WORDS_DEFAULT];

/* =========================================================================
 * task_led
 *
 * Blinks the Nucleo user LED (PA5) at 1 Hz.
 * Demonstrates that the scheduler is preempting and resuming tasks.
 * ========================================================================= */
static void task_led(void)
{
    while (1) {
        GPIO_SET(LED_PORT, LED_PIN);    /* LED on                            */
        osDelay(500);                   /* 500 ms — scheduler runs task2 here*/
        GPIO_CLR(LED_PORT, LED_PIN);    /* LED off                           */
        osDelay(500);                   /* 500 ms — scheduler runs task2 here*/
    }
}

/* =========================================================================
 * task_counter
 *
 * Increments a volatile counter every ~10 ms.
 * Watch "task2_counter" in the debugger Live Expressions window.
 * If it increments while the LED is blinking, both tasks are running.
 * ========================================================================= */
volatile uint32_t task2_counter = 0;

static void task_counter(void)
{
    while (1) {
        task2_counter++;
        osDelay(10);    /* yield for 10 ms between increments               */
    }
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    /* ---- Hardware init ---- */
    SystemClock_Config();   /* SYSCLK = 48 MHz                               */
    GPIO_Init();            /* PA5 = push-pull output, LED off               */
    SysTick_Init();         /* 1 kHz tick, PendSV at lowest priority         */

    /* Global interrupts must be enabled before osStart() triggers PendSV.
     * SysTick_Handler checks rtos_started before touching PendSV, so this
     * is safe to call before osTaskCreate(). */
    __CPSIE_I();

    /* ---- Create tasks ---- */
    osTaskCreate("led",     task_led,     task_led_stack,   TASK_STACK_WORDS_DEFAULT, 1U);
    osTaskCreate("counter", task_counter, task_count_stack, TASK_STACK_WORDS_DEFAULT, 1U);

    /* ---- Start the RTOS (never returns) ---- */
    osStart();

    /* Unreachable */
}
