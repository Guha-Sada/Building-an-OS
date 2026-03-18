/**
 * @file    context_switch.s
 * @brief   Cortex-M0+ context switch — PendSV_Handler and _rtos_start
 *
 * WHY ASSEMBLY?
 * C cannot directly access most ARM system registers (PSP, CONTROL, MSR/MRS)
 * or express the precise register save/restore ordering the context switch
 * demands.  This file is the only assembly in the project; everything else
 * is plain C.
 *
 * M0+ CONSTRAINTS (vs M3/M4)
 * - PUSH / POP only work with R0-R7 and LR/PC.  High registers (R8-R11)
 *   must be moved to R0-R7 via MOV before STMIA / LDMIA.
 * - No STMDB — use SUBS + STMIA instead.
 * - No exclusive load/store (LDREX/STREX) — use CPSID/CPSIE for atomics.
 * - Only 2 priority bits are implemented (bits [7:6] of each priority byte).
 *
 * STACK FRAME LAYOUT (ascending addresses at TCB->sp after save):
 *
 *   TCB->sp + 0  : R4   \
 *   TCB->sp + 4  : R5    |
 *   TCB->sp + 8  : R6    |  software-saved by this handler
 *   TCB->sp + 12 : R7    |
 *   TCB->sp + 16 : R8    |
 *   TCB->sp + 20 : R9    |
 *   TCB->sp + 24 : R10   |
 *   TCB->sp + 28 : R11  /
 *   TCB->sp + 32 : R0   \
 *   TCB->sp + 36 : R1    |
 *   TCB->sp + 40 : R2    |  hardware auto-saved on exception entry
 *   TCB->sp + 44 : R3    |
 *   TCB->sp + 48 : R12   |
 *   TCB->sp + 52 : LR    |
 *   TCB->sp + 56 : PC    |
 *   TCB->sp + 60 : xPSR /   <- highest address of frame
 *
 * FIRST SWITCH BOOTSTRAPPING
 * The very first PendSV (triggered manually by _rtos_start) must not try to
 * save any outgoing context — there is none.  The rtos_started flag (0 until
 * set here) gates the save path.  osStart() (C wrapper in rtos.c) triggers
 * PendSV after switching thread mode to PSP; PendSV sees rtos_started==0,
 * skips the save, restores task 0, and execution transfers to the first task.
 */

.syntax unified
.cpu    cortex-m0plus
.fpu    softvfp
.thumb

/* ---- Symbols defined in rtos.c / scheduler.c that we reference ---------- */
.extern current_task        /* TCB_t * — pointer to current task's TCB      */
.extern rtos_started        /* uint8_t — 0 = skip save on first PendSV      */
.extern os_dummy_stack      /* uint32_t[8] — scratch PSP target for osStart  */
.extern os_schedule         /* void os_schedule(void) — picks next task      */

/* ---- Symbols defined here, visible to the rest of the project ----------- */
.global PendSV_Handler
.global _rtos_start

/* ARM Cortex-M System Control Block — addresses are architectural constants */
.equ SCB_ICSR_ADDR,      0xE000ED04   /* Interrupt Control and State Reg    */
.equ SCB_ICSR_PENDSVSET, 0x10000000   /* Bit 28 — set to pend PendSV        */

/* =========================================================================
 * PendSV_Handler
 *
 * Triggered at lowest IRQ priority (0xC0) by SysTick_Handler every 1 ms,
 * and once manually by _rtos_start to kick off the first context switch.
 *
 * Execution path:
 *   1. Disable interrupts (CPSID I).
 *   2. Check rtos_started.  If 0 → first switch: skip save, set flag, jump.
 *   3. SAVE  : pull PSP, reserve 32 bytes below hw-frame, store R4-R11.
 *   4. PICK  : call os_schedule() (C) to update current_task.
 *   5. RESTORE: load new TCB->sp, recover R8-R11 then R4-R7, set new PSP.
 *   6. CPSIE I, BX LR (EXC_RETURN → hardware pops R0-R3, R12, LR, PC, xPSR).
 * ========================================================================= */

    .section .text.PendSV_Handler, "ax", %progbits
    .type   PendSV_Handler, %function

PendSV_Handler:

    CPSID   I                       /* Disable interrupts for atomic switch  */

    /* -----------------------------------------------------------------------
     * First-switch guard: skip saving the bootstrap context.
     * ----------------------------------------------------------------------- */
    LDR     R0, =rtos_started
    LDRB    R1, [R0]
    CMP     R1, #0
    BNE     pendsv_save             /* Non-zero → normal save/restore        */

    MOVS    R1, #1
    STRB    R1, [R0]                /* rtos_started = 1                      */
    B       pendsv_restore          /* Jump straight to loading the first task*/

    /* -----------------------------------------------------------------------
     * SAVE — push R4-R11 below the hardware-saved frame.
     *
     * On exception entry the CPU already pushed {R0-R3, R12, LR, PC, xPSR}
     * onto the PSP.  PSP now points to the R0 slot.  We push R4-R11 by
     * decrementing PSP by 32 and writing with STMIA.
     * ----------------------------------------------------------------------- */
pendsv_save:

    MRS     R0, PSP                 /* R0 = outgoing PSP (→ top of hw frame) */
    SUBS    R0, R0, #32             /* R0 = PSP - 32  (new TCB->sp)          */

    STMIA   R0!, {R4-R7}            /* Store R4-R7 at [R0+0 .. R0+12]        */
                                    /* R0 is now PSP - 16                    */

    MOV     R4, R8                  /* M0+ cannot STMIA high registers       */
    MOV     R5, R9                  /* directly — copy to R4-R7 first        */
    MOV     R6, R10
    MOV     R7, R11
    STMIA   R0!, {R4-R7}            /* Store R8-R11 at [R0+0 .. R0+12]       */
                                    /* R0 is now PSP (back to hw frame base) */

    SUBS    R0, R0, #32             /* R0 = new TCB->sp = PSP - 32           */

    LDR     R1, =current_task       /* R1 = &current_task                    */
    LDR     R1, [R1]                /* R1 = current_task (TCB pointer)       */
    STR     R0, [R1, #0]            /* TCB->sp = R0  (sp is at offset 0)     */

    /* -----------------------------------------------------------------------
     * PICK — call os_schedule() to select the next task.
     *
     * BL overwrites LR with the return address, but LR currently holds the
     * EXC_RETURN value (0xFFFFFFFD) that we need for the final BX LR.
     * Save it on MSP (handler mode stack) first.
     * ----------------------------------------------------------------------- */
pendsv_restore:

    PUSH    {LR}                    /* Save EXC_RETURN onto MSP              */
    BL      os_schedule             /* current_task = next READY task        */
    POP     {R4}                    /* Pop saved EXC_RETURN into R4          */
    MOV     LR, R4                  /* Restore EXC_RETURN to LR (MOV allows high-reg dst) */

    /* -----------------------------------------------------------------------
     * RESTORE — load R4-R11 from the new task's stack, then update PSP.
     *
     * Memory at TCB->sp (ascending): R4 R5 R6 R7 R8 R9 R10 R11 <hw frame>
     *
     * We restore R8-R11 first (through R4-R7 temporaries), then R4-R7.
     * This avoids clobbering R4-R7 before their correct values are loaded.
     * ----------------------------------------------------------------------- */

    LDR     R1, =current_task
    LDR     R1, [R1]                /* R1 = new current_task                 */
    LDR     R0, [R1, #0]            /* R0 = new TCB->sp                      */

    /* Step A: load saved R8-R11 (at offsets +16..+28) into R4-R7 as temps  */
    ADDS    R0, R0, #16             /* R0 → saved-R8 slot                    */
    LDMIA   R0!, {R4-R7}            /* R4=sv_R8 R5=sv_R9 R6=sv_R10 R7=sv_R11 */
                                    /* R0 now = TCB->sp + 32 (hw frame base) */
    MOV     R8,  R4
    MOV     R9,  R5
    MOV     R10, R6
    MOV     R11, R7

    /* Step B: restore R4-R7 from offsets +0..+12 (back up to TCB->sp)      */
    SUBS    R0, R0, #32             /* R0 = TCB->sp again                    */
    LDMIA   R0!, {R4-R7}            /* Restore R4-R7; R0 advances to sp+16   */

    /* Step C: advance R0 past the R8-R11 save area → base of hw frame      */
    ADDS    R0, R0, #16             /* R0 = TCB->sp + 32 = hw frame base     */

    MSR     PSP, R0                 /* PSP → new task's hardware frame       */

    CPSIE   I                       /* Re-enable interrupts                  */

    /*
     * BX LR with EXC_RETURN = 0xFFFFFFFD:
     *   → Return to Thread mode
     *   → Use PSP as stack pointer
     *   → CPU auto-pops R0, R1, R2, R3, R12, LR, PC, xPSR from PSP
     *   → PC = task entry (first run) or wherever the task was preempted
     */
    BX      LR

    .size PendSV_Handler, .-PendSV_Handler


/* =========================================================================
 * _rtos_start
 *
 * Called from the osStart() C wrapper in rtos.c after current_task and
 * current_task_index have been initialised to the "last task" sentinel.
 * Never returns.
 *
 * Steps:
 *   1. Point PSP at the top of os_dummy_stack (valid RAM for the auto-save
 *      that fires when PendSV interrupts this function's spin loop).
 *   2. Switch thread mode to PSP: CONTROL[1] (SPSEL) = 1.
 *   3. ISB — required after writing CONTROL on Cortex-M0+.
 *   4. Set PENDSVSET in SCB->ICSR to pend PendSV immediately.
 *      (Interrupts are already enabled from main(); PendSV fires as soon as
 *      this thread exits the current exception context — i.e., right after
 *      the ISB following the ICSR write.)
 *   5. Spin.  PendSV fires, sets rtos_started=1, restores task 0.
 *      Execution never returns here.
 * ========================================================================= */

    .section .text._rtos_start, "ax", %progbits
    .type   _rtos_start, %function

_rtos_start:

    /* PSP ← top of scratch buffer                                          */
    LDR     R0, =os_dummy_stack
    ADDS    R0, R0, #32             /* Top of 8-word (32-byte) buffer        */
    MSR     PSP, R0

    /* Switch thread mode to PSP: CONTROL.SPSEL = 1                        */
    MOVS    R0, #2
    MSR     CONTROL, R0
    ISB                             /* Flush pipeline after CONTROL write    */

    /* Pend PendSV — it fires immediately since interrupts are enabled      */
    LDR     R0, =SCB_ICSR_ADDR
    MOVS    R1, #1
    LSLS    R1, R1, #28             /* R1 = 0x10000000 (PENDSVSET bit)       */
    STR     R1, [R0]
    DSB                             /* Ensure the store is visible           */
    ISB                             /* PendSV fires here (or at next instr)  */

    /* Safety spin — should never execute                                   */
_rtos_start_spin:
    B       _rtos_start_spin

    .size _rtos_start, .-_rtos_start
