/**
 * @file    scheduler.c
 * @brief   Priority-based preemptive task scheduler — Phase 7.
 *
 * os_schedule() is called from PendSV_Handler (assembly) on every context
 * switch.  It selects the highest-priority READY task to run next.
 *
 *
 * SCHEDULING POLICY
 * -----------------
 * Strict priority with round-robin fairness within the same level:
 *
 *   1. The READY task with the highest `priority` value always wins.
 *      (priority 0 = lowest / idle, 255 = highest).
 *
 *   2. When multiple READY tasks share the highest priority, the one
 *      that comes next in creation order is chosen (round-robin).
 *      This gives each same-priority task a fair 1 ms time slice.
 *
 *   3. A newly unblocked task (woken by osSemaphoreSignal or the
 *      SysTick wake loop) competes immediately on the next PendSV.
 *      If its priority exceeds the currently running task it will
 *      preempt within at most 1 ms (next SysTick-triggered PendSV),
 *      or immediately when osSemaphoreSignal() calls trigger_pendsv().
 *
 *
 * ALGORITHM — two O(N) passes, N ≤ MAX_TASKS (8)
 * -----------------------------------------------
 *   Pass 1 : scan task_pool[] to find the maximum priority among all
 *            tasks in the TASK_READY state.
 *   Pass 2 : starting one slot after current_task_index (round-robin
 *            wrap), pick the first READY task that matches that maximum
 *            priority.
 *
 *
 * DESIGN CONSTRAINTS
 * ------------------
 * - Called with interrupts disabled (CPSID I held by PendSV_Handler).
 * - Runs on MSP (handler mode); must not touch PSP or call osDelay().
 * - No dynamic allocation — task_pool[] is a fixed static array.
 */

#include "scheduler.h"

/* =========================================================================
 * Scheduler state
 * ========================================================================= */

/** Index of the currently running task in task_pool[].
 *  Initialised to task_count-1 by osStart() so the round-robin search
 *  wraps to task[0] on the very first context switch. */
uint8_t current_task_index = 0;


/* =========================================================================
 * os_schedule
 *
 * Selects the next task and updates current_task / current_task_index.
 * Called by PendSV_Handler (assembly) with interrupts already disabled.
 * ========================================================================= */
void os_schedule(void)
{
    /* ------------------------------------------------------------------
     * Step 0: Release the outgoing task back into the READY pool.
     *
     * Only mark READY if it was still RUNNING (i.e. it was preempted by
     * SysTick).  Tasks that blocked themselves (osDelay, semaphore wait)
     * are already SLEEPING or BLOCKED — leave those states unchanged so
     * the scheduler skips them until they are explicitly woken.
     * ------------------------------------------------------------------ */
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    /* ------------------------------------------------------------------
     * Pass 1: find the highest priority level that has at least one
     * READY task.
     * ------------------------------------------------------------------ */
    uint8_t max_prio  = 0U;
    uint8_t any_ready = 0U;

    for (uint8_t i = 0U; i < task_count; i++) {
        if (task_pool[i].state == TASK_READY) {
            if ((any_ready == 0U) || (task_pool[i].priority > max_prio)) {
                max_prio  = task_pool[i].priority;
                any_ready = 1U;
            }
        }
    }

    /* ------------------------------------------------------------------
     * Pass 2: among tasks at max_prio, take the next one in round-robin
     * order — start one slot after current_task_index and wrap around.
     *
     * This guarantees:
     *   a) If a higher-priority task is READY it is always selected.
     *   b) Tasks at the same priority level share time slices equally.
     * ------------------------------------------------------------------ */
    if (any_ready) {
        for (uint8_t i = 1U; i <= task_count; i++) {
            uint8_t idx = (uint8_t)((current_task_index + i) % task_count);

            if ((task_pool[idx].state    == TASK_READY) &&
                (task_pool[idx].priority == max_prio)) {

                current_task_index  = idx;
                current_task        = &task_pool[idx];
                current_task->state = TASK_RUNNING;
                current_task->run_count++;
                return;
            }
        }
    }

    /* ------------------------------------------------------------------
     * Fallback: no READY task was found (all others SLEEPING or BLOCKED).
     *
     * With an idle task always at priority 0 this branch should never
     * be reached during normal operation.  If it is, keep the current
     * task running if it is still READY; otherwise leave the state alone
     * (SLEEPING/BLOCKED) and wait for the next SysTick or semaphore
     * signal to promote a task back to READY.
     * ------------------------------------------------------------------ */
    if (current_task->state == TASK_READY) {
        current_task->state = TASK_RUNNING;
    }
}
