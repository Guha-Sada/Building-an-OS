/**
 * @file    semaphore.c
 * @brief   Counting semaphore and mutex implementation — Phase 6.
 *
 * Both primitives run on a Cortex-M0+ which has no hardware
 * load-exclusive / store-exclusive instructions (LDREX/STREX).
 * Atomicity is achieved with CPSID I / CPSIE I (global interrupt
 * masking).  Critical sections are kept as short as possible — only
 * the in-memory state updates are protected, not the yield call.
 *
 *
 * SEMAPHORE STATE MACHINE
 * -----------------------
 *
 *  osSemaphoreSignal           osSemaphoreWait
 *  (no waiters → count++)      (count > 0 → count--, return)
 *         ↑                             ↓
 *   count == 0                    count == 0
 *         ↑                             ↓
 *  Signal sees waiter          Wait: add to waiting[], BLOCKED
 *  → READY waiter directly     → trigger_pendsv (yield)
 *  → trigger_pendsv            → resumes here when unblocked
 *
 *
 * WAIT QUEUE POLICY
 * -----------------
 * FIFO: tasks are appended at waiting[wait_count] and dequeued from
 * waiting[0], with a simple shift-up on each dequeue.  This is O(N)
 * per signal but N ≤ MAX_TASKS (8), so it is negligible on M0+.
 * Priority-ordered unblocking can be added in Phase 7 without
 * changing the public API.
 *
 *
 * MUTEX OWNERSHIP
 * ---------------
 * A mutex is a binary semaphore (initial count = 1) plus an owner
 * pointer.  Only the owning task may call osMutexRelease(); a
 * violation traps in an infinite loop so a debugger can catch it.
 * Priority inheritance is a Phase 7+ enhancement.
 */

#include "semaphore.h"      /* struct Semaphore, struct Mutex, MAX_TASKS      */
#include "rtos.h"           /* osSemaphoreXxx / osMutexXxx prototypes         */
#include "stm32c031c6.h"    /* __CPSID_I, __CPSIE_I, trigger_pendsv, __NOP   */
#include <stddef.h>         /* NULL                                           */

/* current_task — defined in rtos.c; needed to record mutex ownership. */
extern TCB_t *current_task;


/* =========================================================================
 * osSemaphoreInit
 *
 * Initialise the semaphore to `initial_count` with an empty wait queue.
 * Must be called before any task uses the semaphore.
 * ========================================================================= */
void osSemaphoreInit(Semaphore_t *s, int32_t initial_count)
{
    s->count      = initial_count;
    s->wait_count = 0U;
    for (uint8_t i = 0U; i < MAX_TASKS; i++) {
        s->waiting[i] = NULL;
    }
}


/* =========================================================================
 * osSemaphoreWait  (P / acquire / "down")
 *
 * Fast path  — count > 0: decrement and return immediately (no context
 *              switch, no blocking).
 *
 * Slow path  — count == 0: append the calling task to the wait queue,
 *              set its state to TASK_BLOCKED, yield via trigger_pendsv().
 *              Execution resumes here after osSemaphoreSignal() has
 *              directly promoted this task back to TASK_READY and a
 *              PendSV has rescheduled it.  No extra decrement is needed
 *              on return: the resource was handed directly to this task
 *              by the signaller (count was never incremented).
 * ========================================================================= */
void osSemaphoreWait(Semaphore_t *s)
{
    __CPSID_I();                            /* ---- critical section open    */

    if (s->count > 0) {
        s->count--;
        __CPSIE_I();                        /* ---- critical section close   */
        return;                             /* Acquired immediately           */
    }

    /* Count is 0 — block the calling task */
    s->waiting[s->wait_count] = current_task;
    s->wait_count++;
    current_task->state = TASK_BLOCKED;

    __CPSIE_I();                            /* ---- critical section close   */

    /*
     * Yield to another task.  PendSV_Handler saves our context and calls
     * os_schedule() which picks the next READY task.  We are not READY
     * (state == TASK_BLOCKED) so we will not be selected until
     * osSemaphoreSignal() sets our state back to TASK_READY.
     *
     * When we are eventually rescheduled, execution continues at the
     * instruction after trigger_pendsv() — i.e., we fall through to the
     * closing brace and return to the caller with the semaphore acquired.
     */
    trigger_pendsv();
}


/* =========================================================================
 * osSemaphoreSignal  (V / release / "up")
 *
 * If a task is waiting: dequeue the head of the wait queue, set its
 * state to TASK_READY, and pend PendSV so it can run on the next slice.
 * The count is NOT incremented in this case — the resource is handed
 * directly from the signaller to the first waiter.
 *
 * If nobody is waiting: increment count so the next osSemaphoreWait()
 * can take the fast path.
 *
 * Callable from both task context and ISR context (SysTick_Handler, etc.)
 * as long as the semaphore was initialised before the ISR fires.
 * ========================================================================= */
void osSemaphoreSignal(Semaphore_t *s)
{
    __CPSID_I();                            /* ---- critical section open    */

    if (s->wait_count > 0U) {

        /* Dequeue the longest-waiting task (FIFO) */
        TCB_t *unblocked = s->waiting[0];

        /* Shift the remaining waiters one slot forward */
        for (uint8_t i = 0U; i < (uint8_t)(s->wait_count - 1U); i++) {
            s->waiting[i] = s->waiting[i + 1U];
        }
        s->wait_count--;
        s->waiting[s->wait_count] = NULL;   /* Clear vacated slot             */

        /* Make the unblocked task eligible to run */
        unblocked->state = TASK_READY;

        __CPSIE_I();                        /* ---- critical section close   */

        /*
         * Pend a context switch so the newly-ready task can compete for
         * the CPU without waiting for the next SysTick edge.  In a
         * priority scheduler (Phase 7) this is where preemption happens
         * if the unblocked task has higher priority than the signaller.
         */
        trigger_pendsv();

    } else {
        /* No waiters — just increment the count */
        s->count++;
        __CPSIE_I();                        /* ---- critical section close   */
    }
}


/* =========================================================================
 * osMutexInit
 *
 * A mutex is a binary semaphore (initial count = 1, i.e. "unlocked")
 * plus an owner field that enforces release-by-owner-only.
 * ========================================================================= */
void osMutexInit(Mutex_t *m)
{
    osSemaphoreInit(&m->sem, 1);    /* 1 = unlocked, 0 = locked               */
    m->owner = NULL;
}


/* =========================================================================
 * osMutexAcquire
 *
 * Blocks until the mutex is free, then records the calling task as owner.
 * After osSemaphoreWait() returns the semaphore count is 0, so no other
 * task can acquire the mutex between Wait() returning and us setting owner.
 * ========================================================================= */
void osMutexAcquire(Mutex_t *m)
{
    osSemaphoreWait(&m->sem);       /* Blocks if count == 0 (locked)          */

    /* Record ownership.  We disable interrupts for a clean write even though
     * no concurrent acquire is possible at this point (count == 0). */
    __CPSID_I();
    m->owner = current_task;
    __CPSIE_I();
}


/* =========================================================================
 * osMutexRelease
 *
 * Clears the owner field and signals the underlying semaphore.
 * Traps if called by a task that does not own the mutex — this is always
 * a programming error and should be caught in development.
 * ========================================================================= */
void osMutexRelease(Mutex_t *m)
{
    __CPSID_I();

    if (m->owner != current_task) {
        /* Programming error: release by non-owner.
         * Spin with interrupts re-enabled so a debugger can attach. */
        __CPSIE_I();
        while (1) { __NOP(); }
    }

    m->owner = NULL;
    __CPSIE_I();

    osSemaphoreSignal(&m->sem);     /* Unlock; wake a waiter if any           */
}
