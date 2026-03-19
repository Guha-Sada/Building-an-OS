/**
 * @file    scheduler.c
 * @brief   Round-robin task scheduler (Phase 3-4).
 *
 * os_schedule() is called from PendSV_Handler (assembly) each time the CPU
 * context-switches.  It updates current_task to the next task that should
 * run, cycling through all READY tasks in creation order.
 *
 * Phase 3-4 : Round-robin — each READY task gets equal 1 ms time slices.
 * Phase 7   : Upgrade to priority-based preemptive scheduling.
 *
 * DESIGN NOTES
 * - os_schedule() runs in handler context (MSP, interrupts disabled by
 *   CPSID I in PendSV_Handler).  Keep it short; no blocking operations.
 * - current_task_index tracks the slot we most recently ran so the search
 *   starts one position after it on each call.
 * - On the very first call (bootstrapped by osStart), current_task and
 *   current_task_index are set to the LAST created task by osStart().
 *   Searching one slot forward wraps to task[0], so task[0] always runs first.
 */

#include "scheduler.h"

/* =========================================================================
 * Scheduler state
 * ========================================================================= */

/** Index of current_task inside task_pool[].
 *  Initialised to task_count-1 by osStart() so task[0] runs first. */
uint8_t current_task_index = 0;

/* =========================================================================
 * os_schedule
 *
 * Called from PendSV_Handler with interrupts disabled (CPSID I active).
 * Must update current_task before returning.
 * ========================================================================= */
void os_schedule(void)
{
    /* If the outgoing task was RUNNING, mark it READY so it can be picked
     * again.  Do not change the state if it is SLEEPING or BLOCKED — those
     * states are set intentionally by osDelay() / semaphore wait. */
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    /* Round-robin search: start one slot after the current task, wrap at
     * task_count.  We scan at most task_count slots (a full circle). */
    for (uint8_t i = 1U; i <= task_count; i++) {
        uint8_t idx = (uint8_t)((current_task_index + i) % task_count);

        if (task_pool[idx].state == TASK_READY) {
            current_task_index = idx;
            current_task       = &task_pool[idx];
            current_task->state = TASK_RUNNING;
            return;
        }
    }

    /* No other task is READY.  Keep the current task running.
     *
     * This can happen legitimately if all other tasks are SLEEPING or BLOCKED.
     * If the current task itself is SLEEPING/BLOCKED (and there is no idle task),
     * the CPU busy-waits here until SysTick wakes a sleeping task.  Adding an
     * idle task (Phase 5) prevents this degenerate case. */
    if (current_task->state == TASK_READY) {
        /* Was just marked READY above — flip back to RUNNING */
        current_task->state = TASK_RUNNING;
    }
    /* Otherwise state is already SLEEPING or BLOCKED — leave it unchanged.
     * The next SysTick / semaphore signal will set a task to READY and the
     * scheduler will find it on the following PendSV. */
}
