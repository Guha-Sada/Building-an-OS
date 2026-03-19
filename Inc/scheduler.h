/**
 * @file    scheduler.h
 * @brief   Internal RTOS types — TCB, task states, and global scheduler state.
 *
 * This header is for RTOS-internal use (rtos.c, scheduler.c, context_switch.s).
 * Application code should include rtos.h instead.
 *
 * CRITICAL LAYOUT RULE
 * --------------------
 * TCB_t::sp MUST remain at offset 0 (the very first field).
 * The assembly context-switch code in context_switch.s does:
 *
 *     LDR R1, =current_task   ; R1 = &current_task
 *     LDR R1, [R1]            ; R1 = current_task  (pointer to TCB)
 *     STR R0, [R1, #0]        ; store new SP at offset 0 of TCB
 *
 * If sp moves to any other offset the context switch will silently corrupt
 * unrelated fields and produce very hard-to-diagnose crashes.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/* =========================================================================
 * Limits
 * ========================================================================= */

/** Maximum number of tasks the RTOS can manage simultaneously.
 *  With 12 KB RAM and ~256-byte stacks, 8 tasks consumes ~2 KB just for
 *  stacks, leaving headroom for code, globals, and ISR stack. */
#define MAX_TASKS   8U

/** Recommended per-task stack size in words (uint32_t).
 *  256 bytes = 64 words.  Enough for ~4–6 levels of function nesting.
 *  Increase for tasks that call deeply or use large local arrays. */
#define TASK_STACK_WORDS_DEFAULT    64U

/** Sentinel word written to every unused stack word at task creation.
 *  osGetTaskStats() scans from the bottom of the stack upward and counts
 *  consecutive sentinel words to compute the high-water mark.
 *  Value chosen to be visually obvious in a memory view and unlikely to
 *  occur as legitimate stack data. */
#define OS_STACK_SENTINEL   0xA5A5A5A5UL

/* =========================================================================
 * Task state machine
 *
 *   +--------+   osTaskCreate()   +---------+
 *   |  (new) | -----------------> |  READY  | <---+
 *   +--------+                    +---------+     |
 *                                      |           | wake_tick reached
 *                              select  |           | (SysTick_Handler)
 *                                      v           |
 *                                 +---------+   +-----------+
 *                                 | RUNNING | -> | SLEEPING  |  osDelay()
 *                                 +---------+   +-----------+
 *                                      |
 *                                      |  osSemaphoreWait() with count==0
 *                                      v
 *                                 +---------+
 *                                 | BLOCKED |  (waiting on semaphore)
 *                                 +---------+
 *                                      |
 *                                      | osSemaphoreSignal()
 *                                      v
 *                                  READY  ------>  ...
 * ========================================================================= */
typedef enum {
    TASK_READY    = 0,  /*!< In the run queue, eligible to be scheduled      */
    TASK_RUNNING  = 1,  /*!< Currently executing on the CPU                  */
    TASK_SLEEPING = 2,  /*!< Blocked in osDelay(); wakes at wake_tick        */
    TASK_BLOCKED  = 3,  /*!< Blocked on a semaphore or mutex                 */
} TaskState_t;

/* =========================================================================
 * Task Control Block (TCB)
 *
 * One TCB per task.  All TCBs live in the statically allocated task_pool[].
 * ========================================================================= */
typedef struct TCB {
    /*
     * !!  sp MUST be the very first field — offset 0  !!
     *
     * Saved stack pointer.  When a task is not RUNNING, this holds the PSP
     * value at the moment the task was last context-switched out.  The stack
     * frame at this address looks like:
     *
     *   sp+ 0 : R4   \
     *   sp+ 4 : R5    |
     *   sp+ 8 : R6    |  software-saved (by PendSV_Handler)
     *   sp+12 : R7    |
     *   sp+16 : R8    |
     *   sp+20 : R9    |
     *   sp+24 : R10   |
     *   sp+28 : R11  /
     *   sp+32 : R0   \
     *   sp+36 : R1    |
     *   sp+40 : R2    |  hardware auto-saved (by Cortex-M0+ exception entry)
     *   sp+44 : R3    |
     *   sp+48 : R12   |
     *   sp+52 : LR    |
     *   sp+56 : PC    |
     *   sp+60 : xPSR /
     */
    uint32_t    *sp;            /*!< [offset 0] Saved PSP — MUST be first!  */

    TaskState_t  state;         /*!< Current task state                      */
    uint8_t      priority;      /*!< Scheduling priority (0=lowest, 255=max) */
    uint8_t      _pad[3];       /*!< Explicit padding (keeps alignment tidy) */

    uint32_t     wake_tick;     /*!< sys_tick value at which sleeping wakes  */

    /* ---- Runtime statistics (updated by SysTick_Handler / os_schedule) ---- */
    uint32_t     run_ticks;     /*!< CPU time: SysTick periods this task ran */
    uint32_t     run_count;     /*!< Times os_schedule() selected this task  */

    uint32_t    *stack_base;    /*!< Lowest address of the stack array       */
    uint32_t     stack_words;   /*!< Stack size in uint32_t words            */

    const char  *name;          /*!< Debug name (string literal, not copied) */
} TCB_t;

/* =========================================================================
 * Global scheduler state
 *
 * Defined in rtos.c; extern-declared here so scheduler.c and
 * context_switch.s can access them.
 * ========================================================================= */

/** Pool of all TCBs.  Indexed 0 … task_count-1. */
extern TCB_t          task_pool[MAX_TASKS];

/** Pointer to the TCB of the task currently on (or being switched to) CPU. */
extern TCB_t         *current_task;

/** Number of tasks created so far (grows monotonically, never shrinks). */
extern uint8_t        task_count;

/** Millisecond tick counter.  Incremented every 1 ms by SysTick_Handler. */
extern volatile uint32_t sys_tick;

/** Index of current_task in task_pool[].
 *  Initialised to task_count-1 by osStart() (C wrapper in rtos.c) so that
 *  os_schedule()'s round-robin search wraps around to task[0] on the first
 *  context switch.  Updated by os_schedule() on every PendSV thereafter. */
extern uint8_t        current_task_index;

/**
 * 0 = osStart() has not yet run; PendSV should skip saving the bootstrap
 *     context and jump straight to restoring the first task.
 * 1 = normal operation; PendSV saves old task, picks new task, restores.
 *
 * Set to 1 by PendSV_Handler on the very first context switch.
 * Defined in rtos.c; referenced by context_switch.s via .extern.
 */
extern uint8_t        rtos_started;

/**
 * Scratch stack used by the osStart() bootstrap.
 *
 * When osStart() switches CONTROL to PSP mode, PSP must point to valid RAM
 * so that the Cortex-M0+ can auto-save the bootstrap context onto it if an
 * exception fires before the first PendSV restores task 0.  This 8-word
 * buffer provides that landing pad; its contents are never used afterwards.
 *
 * Defined in rtos.c; used by context_switch.s.
 */
extern uint32_t       os_dummy_stack[8];

/* =========================================================================
 * Internal scheduler function
 *
 * Called by PendSV_Handler (assembly) to update current_task.
 * Defined in scheduler.c (Phase 5 — round-robin; Phase 8 — priority).
 * ========================================================================= */
void os_schedule(void);

#endif /* SCHEDULER_H */
