/**
 * @file    rtos.c
 * @brief   RTOS core — global state, task creation, tick counter, delay stub.
 *
 * Phases implemented here:
 *   Phase 2 : TCB pool, osTaskCreate(), os_task_stack_init(), osGetTick()
 *   Phase 2 : SysTick_Handler (tick increment — RTOS-owned from here on)
 *   Phase 5 : osDelay() real implementation (currently a spinwait stub)
 *
 * NOT implemented here:
 *   os_schedule()      — scheduler.c  (Phase 5)
 *   PendSV_Handler     — context_switch.s (Phase 3-4, assembly)
 *   osSemaphore*()     — semaphore.c   (Phase 6)
 *   osMutex*()         — semaphore.c   (Phase 6)
 *
 * Phase 3-4 additions:
 *   osStart()          — C wrapper that primes the scheduler, then calls
 *                         _rtos_start() (assembly) to flip to PSP and fire PendSV.
 *   SysTick_Handler    — now triggers PendSV every tick once RTOS is live.
 */

#include "stm32c031c6.h"
#include "scheduler.h"
#include "rtos.h"
#include <stddef.h>

/* Assembly bootstrap defined in context_switch.s */
extern void _rtos_start(void);

/* =========================================================================
 * Global scheduler state  (extern-declared in scheduler.h)
 * ========================================================================= */

/** All TCBs live here — no dynamic allocation. */
TCB_t         task_pool[MAX_TASKS];

/** Pointer to the TCB of the currently running (or about-to-run) task.
 *  Set to &task_pool[0] by the first osTaskCreate() call.
 *  Updated by os_schedule() on every PendSV. */
TCB_t        *current_task = NULL;

/** Number of tasks created so far. */
uint8_t       task_count   = 0;

/** Millisecond tick counter.  Written only in SysTick_Handler (ISR context).
 *  Read by osGetTick() and osDelay() in thread context — must be volatile. */
volatile uint32_t sys_tick = 0;

/** Bootstrap flag for PendSV_Handler in context_switch.s.
 *  0 = first PendSV should skip the save step (no valid current context yet).
 *  1 = normal operation — save current task before restoring next. */
uint8_t rtos_started = 0;

/** Scratch landing pad for the auto-save that fires when the first PendSV
 *  interrupts the osStart() spin loop.  Must be 8-byte aligned (AAPCS
 *  requires the stack to be 8-byte aligned at public interfaces). */
__attribute__((aligned(8)))
uint32_t os_dummy_stack[8];

/* =========================================================================
 * SysTick_Handler  (overrides the weak alias in startup_stm32c031c6tx.s)
 *
 * This is the RTOS heartbeat.  Runs every 1 ms at priority 0x40.
 *
 * Phase 2 : increment sys_tick.
 * Phase 5 : also scan task_pool for sleeping tasks that should wake.
 * Phase 5 : also trigger PendSV to time-slice running tasks.
 * ========================================================================= */
void SysTick_Handler(void)
{
    sys_tick++;

    /* Phase 5: wake sleeping tasks — uncomment when osDelay() is real.
     *
     *  for (uint8_t i = 0; i < task_count; i++) {
     *      if (task_pool[i].state == TASK_SLEEPING &&
     *          (sys_tick - task_pool[i].wake_tick) < 0x80000000UL) {
     *          task_pool[i].state = TASK_READY;
     *      }
     *  }
     */

    /* Trigger a context switch at the end of every 1 ms time-slice.
     * PendSV is at the lowest interrupt priority (0xC0) so it fires only
     * AFTER SysTick returns, never preempting other ISRs mid-execution.
     *
     * rtos_started is 0 until the very first PendSV sets it, so we never
     * touch PendSV before the RTOS bootstrap completes in _rtos_start(). */
    if (rtos_started) {
        trigger_pendsv();
    }
}

/* =========================================================================
 * osGetTick
 * ========================================================================= */

uint32_t osGetTick(void)
{
    return sys_tick;
}

/* =========================================================================
 * osStart  (Phase 3-4)
 *
 * Public entry point called once from main() after all tasks are created.
 * Never returns.
 *
 * We do two things before handing off to the assembly bootstrap:
 *
 *   1. Prime current_task and current_task_index to the LAST created task.
 *      os_schedule() searches ONE slot forward from current_task_index, so
 *      pointing it at task[N-1] ensures task[0] is chosen first.
 *
 *   2. Call _rtos_start() (assembly) which:
 *        a. Points PSP at os_dummy_stack (scratch area for the initial save).
 *        b. Sets CONTROL.SPSEL = 1 (thread mode uses PSP from now on).
 *        c. Writes PENDSVSET to SCB->ICSR — PendSV fires immediately.
 *        d. PendSV_Handler sees rtos_started==0, skips the save, calls
 *           os_schedule(), restores task[0], jumps to its entry function.
 * ========================================================================= */
void osStart(void)
{
    if (task_count > 0U) {
        current_task_index = task_count - 1U;
        current_task       = &task_pool[current_task_index];
    }
    _rtos_start();          /* Does not return */
}

/* =========================================================================
 * os_task_exit
 *
 * Placed in the initial LR slot of every task's fake exception frame.
 * If a task function ever returns (it must not), execution lands here.
 *
 * We disable interrupts and spin so a debugger can catch the violation.
 * In production you could also trigger a hard-fault or watchdog reset.
 * ========================================================================= */
static void os_task_exit(void)
{
    __CPSID_I();    /* Disable interrupts so the debugger gets a clean halt */
    while (1) {
        __NOP();    /* Place a breakpoint here in your IDE                  */
    }
}

/* =========================================================================
 * os_task_stack_init  (internal helper)
 *
 * Pre-fills a task's stack so it looks exactly as it would if the task had
 * already been running and was then context-switched out by PendSV_Handler.
 *
 * Two stacked frames are written (from top-of-stack downward):
 *
 *  ┌─────────────────────────────────────────────────────────────────────┐
 *  │  Hardware exception frame  (auto-saved by CPU on any exception)     │
 *  │  (higher addresses)                                                  │
 *  │   xPSR = 0x01000000  — Thumb bit set; no exception number          │
 *  │   PC   = func         — where the task starts                       │
 *  │   LR   = os_task_exit — trap if the task ever returns               │
 *  │   R12  = 0                                                           │
 *  │   R3   = 0                                                           │
 *  │   R2   = 0                                                           │
 *  │   R1   = 0                                                           │
 *  │   R0   = 0            — task argument (extend to pass arg later)    │
 *  ├─────────────────────────────────────────────────────────────────────┤
 *  │  Software save frame  (saved/restored by PendSV_Handler)            │
 *  │   R11  = 0                                                           │
 *  │   R10  = 0                                                           │
 *  │   R9   = 0                                                           │
 *  │   R8   = 0                                                           │
 *  │   R7   = 0                                                           │
 *  │   R6   = 0                                                           │
 *  │   R5   = 0                                                           │
 *  │   R4   = 0  ← TCB->sp points here  (lowest address, offset 0)      │
 *  └─────────────────────────────────────────────────────────────────────┘
 *  (lower addresses / top-of-stack after init)
 *
 * This layout matches what PendSV_Handler writes/reads on M0+:
 *   SAVE:    SUBS PSP, #32 ; STMIA {R4-R7} ; mov R8-R11→R4-R7 ; STMIA {R4-R7}
 *   RESTORE: LDMIA {R4-R7} ; R4-R7→R8-R11 ; LDMIA {R4-R7} ; MSR PSP
 * ========================================================================= */
static void os_task_stack_init(TCB_t    *tcb,
                                void    (*func)(void),
                                uint32_t *stack,
                                uint32_t  stack_words)
{
    /* sp starts at the top of the stack array (highest address + 1 word).
     * Each *(--sp) = x  pre-decrements before writing (simulates PUSH). */
    uint32_t *sp = stack + stack_words;

    /* ---- Hardware exception frame (filled top-down) ---- */
    *(--sp) = 0x01000000UL;             /* xPSR  : Thumb=1, no exception     */
    *(--sp) = (uint32_t)func;           /* PC    : task entry point           */
    *(--sp) = (uint32_t)os_task_exit;   /* LR    : trap if task returns       */
    *(--sp) = 0x00000000UL;             /* R12   : initialised to zero        */
    *(--sp) = 0x00000000UL;             /* R3    : initialised to zero        */
    *(--sp) = 0x00000000UL;             /* R2    : initialised to zero        */
    *(--sp) = 0x00000000UL;             /* R1    : initialised to zero        */
    *(--sp) = 0x00000000UL;             /* R0    : task arg (zero for now)    */

    /* ---- Software save frame (filled top-down) ---- */
    *(--sp) = 0x00000000UL;             /* R11 */
    *(--sp) = 0x00000000UL;             /* R10 */
    *(--sp) = 0x00000000UL;             /* R9  */
    *(--sp) = 0x00000000UL;             /* R8  */
    *(--sp) = 0x00000000UL;             /* R7  */
    *(--sp) = 0x00000000UL;             /* R6  */
    *(--sp) = 0x00000000UL;             /* R5  */
    *(--sp) = 0x00000000UL;             /* R4  ← TCB->sp will point here     */

    /* Store the resulting stack pointer into the TCB.
     * From this moment the task is "restoreable" by PendSV_Handler. */
    tcb->sp = sp;   /* offset 0 — must remain first field in TCB_t */
}

/* =========================================================================
 * osTaskCreate
 * ========================================================================= */

void osTaskCreate(const char *name,
                  void      (*func)(void),
                  uint32_t   *stack,
                  uint32_t    stack_words,
                  uint8_t     priority)
{
    /* Guard: too many tasks */
    if (task_count >= MAX_TASKS) {
        /* Trap — increase MAX_TASKS or reduce the number of tasks */
        __CPSID_I();
        while (1) { __NOP(); }
    }

    /* Guard: stack too small.  16 words is the bare minimum (8 hw + 8 sw).
     * Anything below 32 words is risky; 64 words is the recommended default. */
    if (stack_words < 16U) {
        __CPSID_I();
        while (1) { __NOP(); }
    }

    /* Disable interrupts around the write to shared state.
     * osTaskCreate() must only be called before osStart(), so the RTOS is
     * not yet running — but defensive practice is good habit. */
    __CPSID_I();

    TCB_t *tcb       = &task_pool[task_count];

    tcb->name        = name;
    tcb->state       = TASK_READY;
    tcb->priority    = priority;
    tcb->wake_tick   = 0U;
    tcb->stack_base  = stack;
    tcb->stack_words = stack_words;

    os_task_stack_init(tcb, func, stack, stack_words);

    /* current_task must always point to a valid TCB.
     * Set it to the first task created; os_schedule() will refine this. */
    if (task_count == 0U) {
        current_task = tcb;
    }

    task_count++;

    __CPSIE_I();
}

/* =========================================================================
 * osDelay  (Phase 3-4: still a spinwait — upgrade in Phase 5)
 *
 * CURRENT BEHAVIOUR: busy-wait.  The calling task monopolises the CPU for
 * the entire delay.  Interrupts remain enabled so SysTick still fires.
 *
 * PHASE 5 REPLACEMENT: set state = TASK_SLEEPING, set wake_tick, trigger
 * PendSV immediately so another READY task runs during the delay.
 * ========================================================================= */
void osDelay(uint32_t ticks)
{
    /*
     * TODO Phase 5: replace the body below with:
     *
     *   __CPSID_I();
     *   current_task->wake_tick = sys_tick + ticks;
     *   current_task->state     = TASK_SLEEPING;
     *   __CPSIE_I();
     *   trigger_pendsv();
     *   // PendSV will context-switch us out; we return here when we wake.
     */
    uint32_t start = sys_tick;
    while ((sys_tick - start) < ticks) {
        /* Interrupts are enabled so SysTick advances sys_tick normally.
         * This is a spinwait and blocks other tasks — temporary until Phase 5. */
    }
}
