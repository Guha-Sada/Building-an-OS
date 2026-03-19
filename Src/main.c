/**
 * @file    main.c
 * @brief   RTOS demo — two tasks scheduled by the round-robin scheduler.
 *
 * Phase 1 : clock init, GPIO, SysTick, LED blink verification.  ✓
 * Phase 2 : sys_tick / SysTick_Handler moved to rtos.c.          ✓
 * Phase 3-4: RTOS starts here — two tasks, context switching live.  ✓
 * Phase 5 : Real osDelay() — tasks genuinely sleep; idle task added. ✓
 * Phase 6 : Semaphores and mutexes implemented; demo added below.    ✓
 *
 * HOW TO VERIFY PHASE 6
 * ---------------------
 * Two new global counters are visible in the Live Expressions window:
 *
 *   sem_signal_count  — incremented by task_led after each blink cycle
 *   sem_consume_count — incremented by task_consumer each time it unblocks
 *
 * Expected observations:
 *   - sem_signal_count and sem_consume_count stay in lock-step: for every
 *     signal there is exactly one consume.  This proves the semaphore
 *     correctly transfers ownership rather than losing or duplicating wakes.
 *   - Both counters increment at ~1 Hz (one blink cycle = 1 second).
 *   - task_consumer spends most of its time BLOCKED (not spinning) —
 *     confirm by pausing the debugger: it should be parked at the
 *     trigger_pendsv() call inside osSemaphoreWait().
 *   - task2_counter still increments at ~100 Hz independently, proving
 *     the semaphore path does not disturb the rest of the scheduler.
 */

#include <stdint.h>
#include "stm32c031c6.h"
#include "scheduler.h"
#include "rtos.h"
#include "semaphore.h"      /* Phase 6: need full struct layout to allocate   */

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
 * Phase 6 — shared semaphore
 *
 * task_led signals this after each complete on/off cycle.
 * task_consumer blocks on it and wakes exactly once per blink.
 * ========================================================================= */
static Semaphore_t sem_blink;               /* Binary-style: init count = 0  */
volatile uint32_t  sem_signal_count  = 0U;  /* How many times led has signalled */
volatile uint32_t  sem_consume_count = 0U;  /* How many times consumer woke up  */

/* =========================================================================
 * Task stacks
 * ========================================================================= */
static uint32_t task_led_stack     [TASK_STACK_WORDS_DEFAULT];  /* 256 B     */
static uint32_t task_count_stack   [TASK_STACK_WORDS_DEFAULT];  /* 256 B     */
static uint32_t task_consumer_stack[TASK_STACK_WORDS_DEFAULT];  /* 256 B     */
static uint32_t task_idle_stack    [32U];                       /* 128 B     */

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
        osDelay(500);
        GPIO_CLR(LED_PORT, LED_PIN);    /* LED off                           */
        osDelay(500);

        /* Phase 6: signal the consumer once per complete blink cycle.
         * osSemaphoreSignal() either increments count (if consumer is not
         * yet waiting) or directly wakes the blocked consumer task. */
        sem_signal_count++;
        osSemaphoreSignal(&sem_blink);
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
 * task_consumer  (Phase 6)
 *
 * Blocks on sem_blink until task_led signals it after each blink cycle.
 * Increments sem_consume_count on every wake — should stay equal to
 * sem_signal_count in the Live Expressions window.
 * ========================================================================= */
static void task_consumer(void)
{
    while (1) {
        osSemaphoreWait(&sem_blink);    /* Block until task_led signals      */
        sem_consume_count++;            /* Prove we ran exactly once per blink*/
    }
}

/* =========================================================================
 * task_idle
 *
 * Runs when every other task is SLEEPING or BLOCKED.  Must never call
 * osDelay() or any blocking primitive — it must always remain READY.
 *
 * __WFI() halts the CPU clock until the next interrupt (SysTick at 1 kHz),
 * reducing power consumption without affecting scheduler correctness: PendSV
 * fires as normal when SysTick_Handler runs and wakes a sleeping task.
 * ========================================================================= */
static void task_idle(void)
{
    while (1) {
        __WFI();    /* Low-power wait; SysTick wakes us every 1 ms at most   */
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

    /* ---- Phase 6: init semaphore before tasks start ---- */
    osSemaphoreInit(&sem_blink, 0);     /* Start at 0: consumer blocks first */

    /* ---- Create tasks ---- */
    osTaskCreate("led",      task_led,      task_led_stack,      TASK_STACK_WORDS_DEFAULT, 1U);
    osTaskCreate("counter",  task_counter,  task_count_stack,    TASK_STACK_WORDS_DEFAULT, 1U);
    osTaskCreate("consumer", task_consumer, task_consumer_stack, TASK_STACK_WORDS_DEFAULT, 1U);
    osTaskCreate("idle",     task_idle,     task_idle_stack,     32U,                      0U);

    /* ---- Start the RTOS (never returns) ---- */
    osStart();

    /* Unreachable */
}
