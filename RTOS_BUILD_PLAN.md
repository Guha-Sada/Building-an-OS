# RTOS Build Plan — STM32C031C6 (Cortex-M0+)
**Target board**: NUCLEO-C031C6
**Core**: ARM Cortex-M0+ @ up to 48MHz
**Memory**: 32KB Flash, 12KB RAM
**Scheduler**: Round-robin first → priority-based
**Features**: Task sleep/delay, semaphores, mutexes
**Approach**: Fully bare-metal (no HAL)

---

## Project File Structure (Target)

```
Inc/
  rtos.h              ← Public RTOS API (task creation, delay, semaphores)
  scheduler.h         ← Internal scheduler types (TCB, task states)
  semaphore.h         ← Semaphore/mutex types and API

Src/
  main.c              ← Application entry + demo tasks
  clock.c             ← Bare-metal system clock init (RCC registers)
  rtos.c              ← Task creation, osDelay, scheduler tick logic
  scheduler.c         ← Round-robin and priority scheduler
  context_switch.s    ← PendSV_Handler + osStart (assembly, M0+-specific)
  semaphore.c         ← Semaphore and mutex implementation
  syscalls.c          ← (keep, required by newlib)
  sysmem.c            ← (keep, required by newlib)

Startup/
  startup_stm32c031c6tx.s  ← (keep as-is; PendSV/SysTick hooks are already weak)
```

---

## Phase 1 — Bare-Metal Foundation

### Step 1: Clock Initialization (`Src/clock.c`)

Replace the placeholder `main.c` HAL calls with bare-metal register access.

- Write `SystemClock_Config()` in `clock.c` using direct RCC register writes
- Configure HSI48 → SYSCLK at 48MHz (the STM32C031C6's maximum)
- No HAL_RCC, no HAL — read `RCC_CR`, `RCC_CFGR`, `FLASH_ACR` directly
- Add a simple `GPIO_Init()` to configure PA5 (the Nucleo's user LED) as output

**Key registers**: `RCC->CR`, `RCC->CFGR`, `FLASH->ACR`, `GPIOA->MODER`, `GPIOA->ODR`

**Verification**: A simple busy-wait LED blink loop in `main()` confirms clocks work
before any RTOS code runs.

---

## Phase 2 — RTOS Core Data Structures

### Step 2: Task Control Block and State Types (`Inc/scheduler.h`)

Every task is represented by a TCB. Define:

```c
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_BLOCKED        // waiting on semaphore
} TaskState_t;

typedef struct TCB {
    uint32_t  *sp;              // Saved stack pointer (MUST be first field)
    TaskState_t state;
    uint8_t    priority;        // 0 = lowest, 255 = highest
    uint32_t   wake_tick;       // SysTick count at which a sleeping task wakes
    uint32_t  *stack_base;      // For stack overflow detection (optional)
    const char *name;           // Debug name
} TCB_t;
```

> **Why `sp` must be first**: The assembly context-switch code does `LDR R0, [R1]`
> where R1 is the TCB pointer, and R0 becomes the task's stack pointer. The struct
> offset of `sp` must be 0.

Also define the global scheduler state:
```c
#define MAX_TASKS       8
extern TCB_t  task_pool[MAX_TASKS];
extern TCB_t *current_task;
extern uint8_t task_count;
extern volatile uint32_t sys_tick;
```

### Step 3: Task Stack Sizing Strategy

With only **12KB RAM**, allocate stacks carefully:

| Purpose | Size |
|---|---|
| Per-task stack | 256 bytes (64 words) — enough for ~4 nested calls |
| MSP (ISR stack) | 512 bytes (kept in linker script's `_Min_Stack_Size`) |
| TCB pool (8 tasks) | ~128 bytes |
| Semaphore structs | ~64 bytes |
| Total RTOS overhead | ~2.5KB, leaving ~9.5KB for code/BSS |

Define stacks as static arrays in the source file that creates each task:
```c
static uint32_t task1_stack[64];  // 256 bytes
```

---

## Phase 3 — SysTick Configuration

### Step 4: SysTick Setup (`Src/rtos.c`)

Write `SysTick_Init(uint32_t ticks_per_second)`:

- Write `SysTick->LOAD` = (SYSCLK / ticks_per_second) - 1
- Write `SysTick->VAL` = 0
- Write `SysTick->CTRL` = enable + tick interrupt + use processor clock

Then override the weak `SysTick_Handler`:

```c
void SysTick_Handler(void) {
    sys_tick++;

    // Wake any sleeping tasks whose delay has expired
    for (int i = 0; i < task_count; i++) {
        if (task_pool[i].state == TASK_SLEEPING &&
            task_pool[i].wake_tick <= sys_tick) {
            task_pool[i].state = TASK_READY;
        }
    }

    // Trigger PendSV to perform the context switch
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}
```

> **Why trigger PendSV from SysTick?** SysTick runs at the highest configurable
> priority. Doing the actual context switch inside SysTick is fragile. PendSV is
> designed to run at the *lowest* interrupt priority — it defers the context switch
> until all higher-priority interrupts are done. This is the standard ARM RTOS pattern.

Also set PendSV to lowest priority:
```c
NVIC_SetPriority(PendSV_IRQn, 0xFF);   // Lowest possible
NVIC_SetPriority(SysTick_IRQn, 0x00);  // Highest
```

---

## Phase 4 — Context Switching (The Core of the RTOS)

### Step 5: Initial Stack Frame Setup (`Src/rtos.c`)

When a task is *created* (not yet run), its stack must look like the CPU already
ran the task and then a context switch happened mid-execution. We fake both frames:

**Cortex-M0+ hardware exception frame** (auto-pushed when entering an exception):
```
[sp+28] xPSR   = 0x01000000  (Thumb bit set, required)
[sp+24] PC     = task_function pointer
[sp+20] LR     = osTaskExit  (called if a task returns; should never happen)
[sp+16] R12    = 0
[sp+12] R3     = 0
[sp+8]  R2     = 0
[sp+4]  R1     = 0
[sp+0]  R0     = 0           (task argument, extend later)
```

**Software-saved registers** (we push these in PendSV, placed below the hw frame):
```
[sp-4]  R11 = 0
[sp-8]  R10 = 0
...
[sp-32] R4  = 0
```

The TCB's `sp` field is set to `stack_top - 64` (16 registers × 4 bytes).

```c
void os_task_stack_init(TCB_t *tcb, void (*func)(void),
                        uint32_t *stack, uint32_t stack_size) {
    uint32_t *sp = stack + stack_size;  // Top of stack

    // Hardware frame (indexes count DOWN from top)
    *(--sp) = 0x01000000;   // xPSR
    *(--sp) = (uint32_t)func;// PC
    *(--sp) = (uint32_t)os_task_exit; // LR
    *(--sp) = 0;             // R12
    *(--sp) = 0;             // R3
    *(--sp) = 0;             // R2
    *(--sp) = 0;             // R1
    *(--sp) = 0;             // R0

    // Software frame (R4-R11)
    *(--sp) = 0;             // R11
    *(--sp) = 0;             // R10
    *(--sp) = 0;             // R9
    *(--sp) = 0;             // R8
    *(--sp) = 0;             // R7
    *(--sp) = 0;             // R6
    *(--sp) = 0;             // R5
    *(--sp) = 0;             // R4

    tcb->sp = sp;  // Save final SP in TCB
}
```

### Step 6: PendSV Context Switch Handler (`Src/context_switch.s`)

This is the most critical piece. Cortex-M0+ has a restricted instruction set vs M3/M4 — notably it **cannot PUSH/POP high registers directly**. They must go through R0-R7 first.

```asm
.syntax unified
.cpu cortex-m0plus
.thumb

.extern current_task
.extern os_schedule         // C scheduler function — returns new current_task

.global PendSV_Handler
.type PendSV_Handler, %function

PendSV_Handler:
    CPSID I                  // 1. Disable interrupts during switch

    // --- SAVE CURRENT TASK ---
    MRS   R0, PSP            // R0 = current task PSP
    SUBS  R0, R0, #32        // Make room for R4-R11 (8 regs × 4 = 32 bytes)

    STMIA R0!, {R4-R7}       // Save R4-R7 to stack (M0+ can only STMIA low regs)
    MOV   R4, R8             // Move high registers into low regs
    MOV   R5, R9
    MOV   R6, R10
    MOV   R7, R11
    STMIA R0!, {R4-R7}       // Save R8-R11 (via R4-R7)
    SUBS  R0, R0, #32        // Reset R0 to point to base of saved area (= new SP)

    LDR   R1, =current_task  // R1 = &current_task (global pointer-to-pointer)
    LDR   R1, [R1]           // R1 = current_task (pointer to TCB)
    STR   R0, [R1, #0]       // TCB->sp = R0 (sp is at offset 0 in struct)

    // --- CALL C SCHEDULER ---
    PUSH  {LR}               // Save EXC_RETURN value
    BL    os_schedule        // C function updates current_task to next task
    POP   {LR}               // Restore EXC_RETURN

    // --- RESTORE NEW TASK ---
    LDR   R1, =current_task
    LDR   R1, [R1]           // R1 = new current_task (pointer to TCB)
    LDR   R0, [R1, #0]       // R0 = new task's saved SP

    LDMIA R0!, {R4-R7}       // Restore R4-R7 (stored first at lower addresses)
    LDMIA R0!, {R3-R6}       // Load R8-R11 values into R3-R6 temporarily
    MOV   R8, R3
    MOV   R9, R4
    MOV   R10, R5
    MOV   R11, R6
    // Note: R4-R7 were already restored above; the second LDMIA gives us
    // R8-R11 via temp registers — adjust addressing carefully in final code

    MSR   PSP, R0            // Update PSP to point to hardware-saved frame
    CPSIE I                  // Re-enable interrupts

    BX    LR                 // Return using EXC_RETURN; hardware auto-restores
                             // R0-R3, R12, LR, PC, xPSR from PSP

.size PendSV_Handler, .-PendSV_Handler
```

> **Note on the above**: The LDMIA sequence for restoring R8-R11 needs a minor
> fixup in the final implementation to avoid clobbering R4-R7 that we just restored.
> The pattern is to do the high-reg restore first, then the low-reg restore. This
> will be refined in Step 6 implementation.

### Step 7: RTOS Start (`Src/context_switch.s`)

`osStart()` bootstraps execution of the first task. We're in privileged thread mode
using MSP. We need to switch to PSP and jump into the first task:

```asm
.global osStart
osStart:
    // Load first task's SP from TCB (current_task must be set before calling)
    LDR   R0, =current_task
    LDR   R0, [R0]
    LDR   R1, [R0, #0]      // R1 = TCB->sp

    // Point PSP past the software regs to the hardware frame
    ADDS  R1, R1, #32       // Skip R4-R11 software save area
    MSR   PSP, R1

    // Switch thread mode to use PSP (CONTROL bit 1 = SPSEL)
    MOVS  R0, #2
    MSR   CONTROL, R0
    ISB                     // Instruction sync barrier required after MSR CONTROL

    // Enable SysTick → first tick will fire PendSV → first context switch
    // (SysTick_Init was called before osStart)
    CPSIE I

    // Spin here — first SysTick will context-switch us into the first task
    B     .
```

---

## Phase 5 — Round-Robin Scheduler

### Step 8: Scheduler Implementation (`Src/scheduler.c`)

```c
// Called from PendSV_Handler (via BL os_schedule)
void os_schedule(void) {
    // Mark currently running task as READY (unless it's SLEEPING/BLOCKED)
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    // Round-robin: find next READY task after current one
    uint8_t start = current_task_index;
    do {
        current_task_index = (current_task_index + 1) % task_count;
        if (task_pool[current_task_index].state == TASK_READY) {
            current_task = &task_pool[current_task_index];
            current_task->state = TASK_RUNNING;
            return;
        }
    } while (current_task_index != start);

    // No other task is READY — keep running current task (or run idle task)
    current_task->state = TASK_RUNNING;
}
```

Also create an **idle task** (always READY, lowest priority, does nothing but WFI):
```c
static void idle_task(void) {
    while (1) { __WFI(); }  // Wait for interrupt — saves power
}
```

### Step 9: Task Creation API (`Src/rtos.c`, `Inc/rtos.h`)

```c
// Public API
void osTaskCreate(const char *name, void (*func)(void),
                  uint32_t *stack, uint32_t stack_size, uint8_t priority);
void osStart(void);   // defined in assembly
```

### Step 10: First Integration Test

Write two demo tasks in `main.c`:

```c
static uint32_t task1_stack[64];
static uint32_t task2_stack[64];

void task1(void) {
    while (1) {
        GPIOA->ODR |=  (1 << 5);   // LED on
        osDelay(500);               // sleep 500 ticks (~500ms)
    }
}

void task2(void) {
    while (1) {
        GPIOA->ODR &= ~(1 << 5);   // LED off
        osDelay(300);
    }
}

int main(void) {
    SystemClock_Config();
    GPIO_Init();
    osTaskCreate("task1", task1, task1_stack, 64, 1);
    osTaskCreate("task2", task2, task2_stack, 64, 1);
    osStart();
}
```

Expected result: LED blinks in a 500ms on / 300ms off pattern.

---

## Phase 6 — Task Sleep / Delay

### Step 11: `osDelay()` Implementation (`Src/rtos.c`)

```c
void osDelay(uint32_t ticks) {
    current_task->wake_tick = sys_tick + ticks;
    current_task->state = TASK_SLEEPING;

    // Trigger immediate reschedule — don't waste this tick
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

    // The scheduler will not pick this task again until wake_tick is reached
    // (SysTick_Handler checks sleeping tasks each tick)
}
```

This is a **blocking delay** — the calling task gives up the CPU immediately. Other
tasks run during the delay period. This is fundamentally different from a busy-wait.

---

## Phase 7 — Semaphores and Mutexes

### Step 12: Semaphore Implementation (`Src/semaphore.c`, `Inc/semaphore.h`)

A **counting semaphore**:

```c
typedef struct {
    volatile int32_t  count;
    TCB_t            *waiting[MAX_TASKS];  // Tasks blocked on this semaphore
    uint8_t           wait_count;
} Semaphore_t;

void osSemaphoreInit(Semaphore_t *s, int32_t initial_count);

// Wait (P / acquire): decrement count; block if count == 0
void osSemaphoreWait(Semaphore_t *s);

// Signal (V / release): increment count; unblock a waiting task if any
void osSemaphoreSignal(Semaphore_t *s);
```

Key logic in `osSemaphoreWait`:
```c
void osSemaphoreWait(Semaphore_t *s) {
    CPSID_I();  // Critical section
    if (s->count > 0) {
        s->count--;
        CPSIE_I();
        return;
    }
    // Block this task
    s->waiting[s->wait_count++] = current_task;
    current_task->state = TASK_BLOCKED;
    CPSIE_I();
    // Trigger reschedule
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}
```

A **mutex** is a binary semaphore (initial count = 1) with an ownership concept:

```c
typedef struct {
    Semaphore_t sem;
    TCB_t      *owner;  // Task that holds the mutex (NULL if free)
} Mutex_t;
```

### Step 13: Critical Section Helpers

Because the Cortex-M0+ lacks `LDREX`/`STREX` (exclusive load/store, which M3/M4
has), atomic operations must use interrupt disable/enable instead:

```c
static inline void os_enter_critical(void) { __asm volatile ("CPSID I"); }
static inline void os_exit_critical(void)  { __asm volatile ("CPSIE I"); }
```

---

## Phase 8 — Priority-Based Preemptive Scheduling

### Step 14: Upgrade the Scheduler (`Src/scheduler.c`)

Replace the round-robin loop with a priority search:

```c
void os_schedule(void) {
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    // Find the highest-priority READY task
    TCB_t *best = NULL;
    for (int i = 0; i < task_count; i++) {
        if (task_pool[i].state == TASK_READY) {
            if (best == NULL || task_pool[i].priority > best->priority) {
                best = &task_pool[i];
            }
        }
    }

    if (best != NULL) {
        current_task = best;
        current_task->state = TASK_RUNNING;
    }
    // else: keep current task running (idle task should always be READY)
}
```

**Among equal-priority tasks**: apply round-robin (track last-run index per priority
level, or simply pick the first match and rotate on next tick).

### Step 15: Priority in Task Creation

Update `osTaskCreate` to store the priority field and assign it per-task.

The idle task always gets priority 0. User tasks start at priority 1+.

---

## Phase 9 — Testing and Validation

### Step 16: Multi-Task Demo

Create 3 tasks at different priorities to verify preemption:

```c
void high_priority_task(void) {
    // Runs whenever it's READY; preempts lower-priority tasks
    while (1) {
        // Toggle LED rapidly for 100ms, then delay
        for (int i = 0; i < 10; i++) {
            GPIOA->ODR ^= (1 << 5);
            osDelay(10);
        }
        osDelay(1000);
    }
}
```

### Step 17: Semaphore Demo

Two tasks sharing a resource, protected by a mutex:

```c
Mutex_t uart_mutex;

void task_a(void) {
    while (1) {
        osMutexAcquire(&uart_mutex);
        // exclusive access to shared resource
        osMutexRelease(&uart_mutex);
        osDelay(200);
    }
}
```

---

## Key Cortex-M0+ Gotchas to Keep in Mind

| Issue | Detail |
|---|---|
| No LDREX/STREX | Use CPSID/CPSIE for atomics (disables all interrupts briefly) |
| No PUSH/POP for R8-R11 | Must MOV high regs to R0-R7 before STMIA/LDMIA |
| Limited addressing | Many instructions only work on R0-R7 in Thumb mode |
| 8-word exception frame | xPSR, PC, LR, R12, R3, R2, R1, R0 (not R4-R11) |
| No divide instruction | Hardware divider not available (use software division) |
| PendSV is key | Always trigger context switch via PendSV, never from SysTick directly |
| ISB after MSR CONTROL | Required when switching between MSP and PSP |

---

## Build Order Summary

| Step | File(s) | What you get |
|---|---|---|
| 1 | `clock.c`, `main.c` | Bare-metal LED blink, no RTOS |
| 2-3 | `scheduler.h` | TCB struct, state enum, global state |
| 4 | `rtos.c` | SysTick init + handler |
| 5 | `rtos.c` | Stack frame initialization |
| 6 | `context_switch.s` | PendSV context switch (assembly) |
| 7 | `context_switch.s` | osStart() bootstrap |
| 8 | `scheduler.c` | Round-robin scheduler |
| 9 | `rtos.c`, `rtos.h` | osTaskCreate() API |
| 10 | `main.c` | First multi-task test |
| 11 | `rtos.c` | osDelay() |
| 12-13 | `semaphore.c/h` | Semaphores + mutexes |
| 14-15 | `scheduler.c` | Priority-based scheduling |
| 16-17 | `main.c` | Integration tests |
