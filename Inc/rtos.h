/**
 * @file    rtos.h
 * @brief   Public RTOS API — task management, delay, semaphores
 *
 * Include this header in any file that uses the RTOS.
 * Internal scheduler types are in scheduler.h.
 *
 * Phases:
 *   Phase 1  — stub only (this file exists so #include "rtos.h" compiles)
 *   Phase 2+ — filled in as each feature is implemented
 */

#ifndef RTOS_H
#define RTOS_H

#include <stdint.h>
#include "scheduler.h"     /* TaskState_t — needed by TaskStats_t below      */

/* =========================================================================
 * Task API  (implemented in Src/rtos.c)
 * ========================================================================= */

/**
 * @brief  Create a task and add it to the scheduler.
 *
 * @param  name       Debug name string (not copied, must be a string literal)
 * @param  func       Task entry function — must loop forever, never return
 * @param  stack      Pointer to the task's statically-allocated stack array
 * @param  stack_size Number of uint32_t words in the stack array
 * @param  priority   Task priority (0 = idle/lowest, higher = more urgent)
 */
void osTaskCreate(const char    *name,
                  void         (*func)(void),
                  uint32_t      *stack,
                  uint32_t       stack_size,
                  uint8_t        priority);

/**
 * @brief  Start the RTOS scheduler (never returns).
 *
 *         Call once after all tasks have been created with osTaskCreate().
 *         Implemented in Src/context_switch.s (assembly bootstrap).
 */
void osStart(void);

/**
 * @brief  Block the calling task for at least `ticks` SysTick periods.
 *
 *         The CPU is yielded immediately — other READY tasks run during
 *         the delay.  Use this instead of a busy-wait inside tasks.
 *
 * @param  ticks  Number of 1 ms ticks to sleep (1 tick ≈ 1 ms at 1 kHz)
 */
void osDelay(uint32_t ticks);

/* =========================================================================
 * Semaphore / Mutex API  (implemented in Src/semaphore.c)
 * ========================================================================= */

/* Forward-declare opaque types — full definitions in semaphore.h */
typedef struct Semaphore Semaphore_t;
typedef struct Mutex     Mutex_t;

/**
 * @brief  Initialise a counting semaphore.
 * @param  s             Pointer to a Semaphore_t struct (caller allocates)
 * @param  initial_count Starting count (e.g. 1 for binary, N for pool)
 */
void osSemaphoreInit(Semaphore_t *s, int32_t initial_count);

/**
 * @brief  Wait (P / acquire).  Block if count == 0.
 */
void osSemaphoreWait(Semaphore_t *s);

/**
 * @brief  Signal (V / release).  Unblock one waiting task if any.
 */
void osSemaphoreSignal(Semaphore_t *s);

/**
 * @brief  Initialise a mutex (binary semaphore with ownership tracking).
 */
void osMutexInit(Mutex_t *m);

/**
 * @brief  Acquire the mutex.  Block if already owned by another task.
 */
void osMutexAcquire(Mutex_t *m);

/**
 * @brief  Release the mutex.  Only the owning task should call this.
 */
void osMutexRelease(Mutex_t *m);

/* =========================================================================
 * System tick accessor
 * ========================================================================= */

/**
 * @brief  Return the current SysTick counter (milliseconds since osStart).
 */
uint32_t osGetTick(void);

/* =========================================================================
 * Runtime statistics API  (implemented in Src/rtos.c)
 * ========================================================================= */

/**
 * @brief  Snapshot of one task's runtime statistics.
 *
 * Obtain by calling osGetTaskStats().  All fields are consistent with each
 * other at the moment of the call (interrupts are briefly disabled).
 *
 * CPU percentage approximation (assuming SysTick at 1 kHz):
 *   cpu_pct = (stats.run_ticks * 100U) / osGetTick();
 *
 * Stack margin in words (how many words remain before overflow):
 *   margin  = stats.stack_total_words - stats.stack_hwm_words;
 */
typedef struct {
    const char  *name;              /*!< Task name string (not copied)        */
    TaskState_t  state;             /*!< Current task state                   */
    uint8_t      priority;          /*!< Scheduling priority (0 = lowest)     */
    uint32_t     run_ticks;         /*!< SysTick periods charged to this task */
    uint32_t     run_count;         /*!< Times selected by os_schedule()      */
    uint32_t     stack_hwm_words;   /*!< Max stack words ever in use          */
    uint32_t     stack_total_words; /*!< Total stack allocation in words      */
} TaskStats_t;

/**
 * @brief  Fill *out with a stats snapshot for the task at index task_idx.
 *
 * @param  task_idx   0-based task index (0 … osGetTaskCount()-1)
 * @param  out        Caller-allocated TaskStats_t to fill
 *
 * Does nothing if task_idx is out of range or out is NULL.
 */
void osGetTaskStats(uint8_t task_idx, TaskStats_t *out);

/**
 * @brief  Return the number of tasks created so far.
 */
uint8_t osGetTaskCount(void);

#endif /* RTOS_H */
