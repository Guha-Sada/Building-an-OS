The goal of this project is to learn how an OS interacts with an Embedded System using the STM32Cube IDE and it's inbuilt Debugger. A secondary goal is to get comfortable using Claude. I am coming into this project with a working knowledge of the Linux Kernel.

After creating a plan with Claude, this is the targetted directory structure:
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
  syscalls.c         
  sysmem.c          

Startup/
  startup_stm32c031c6tx.s  
```
