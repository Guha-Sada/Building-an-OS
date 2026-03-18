/**
 * @file    semaphore.h
 * @brief   Semaphore and mutex struct definitions.
 *
 * rtos.h forward-declares Semaphore_t and Mutex_t as opaque types so that
 * application code can hold pointers to them without needing these internals.
 *
 * Files that actually ALLOCATE a semaphore or mutex (statically, e.g.
 *   static Semaphore_t my_sem;
 * ) must include this header so the compiler knows the struct size.
 *
 * Files that only CALL osSemaphoreWait / osSemaphoreSignal / etc. need only
 * include rtos.h.
 *
 * Implementation: Src/semaphore.c  (Phase 7)
 */

#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <stdint.h>
#include "scheduler.h"     /* for TCB_t, MAX_TASKS */

/* =========================================================================
 * Counting Semaphore
 *
 * Behaviour:
 *   osSemaphoreWait()   — if count > 0: decrement and return immediately.
 *                         if count == 0: add caller to waiting list, set
 *                         state = TASK_BLOCKED, trigger reschedule.
 *
 *   osSemaphoreSignal() — if a task is waiting: move it to READY and
 *                         trigger reschedule (preempt if higher priority).
 *                         if no one waiting: increment count.
 *
 * A binary semaphore is a counting semaphore with initial_count = 1.
 * ========================================================================= */
struct Semaphore {
    volatile int32_t  count;                /*!< Current count (≥ 0)           */
    TCB_t            *waiting[MAX_TASKS];   /*!< Tasks blocked on this sem.     */
    uint8_t           wait_count;           /*!< Number of blocked tasks        */
};

/* =========================================================================
 * Mutex
 *
 * A mutex is a binary semaphore with ownership tracking.  Only the task
 * that called osMutexAcquire() may call osMutexRelease().
 *
 * Ownership prevents accidental double-release and is the foundation for
 * priority inheritance (future enhancement).
 * ========================================================================= */
struct Mutex {
    struct Semaphore  sem;      /*!< Underlying binary semaphore (count=0/1)  */
    TCB_t            *owner;    /*!< Task that holds the mutex (NULL = free)  */
};

/* Note: the typedef names Semaphore_t and Mutex_t are declared in rtos.h.
 * This file provides the struct bodies that complete those forward declarations. */

#endif /* SEMAPHORE_H */
