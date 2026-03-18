################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_SRCS += \
../Src/context_switch.s 

C_SRCS += \
../Src/clock.c \
../Src/main.c \
../Src/rtos.c \
../Src/scheduler.c \
../Src/syscalls.c \
../Src/sysmem.c 

OBJS += \
./Src/clock.o \
./Src/context_switch.o \
./Src/main.o \
./Src/rtos.o \
./Src/scheduler.o \
./Src/syscalls.o \
./Src/sysmem.o 

S_DEPS += \
./Src/context_switch.d 

C_DEPS += \
./Src/clock.d \
./Src/main.d \
./Src/rtos.d \
./Src/scheduler.d \
./Src/syscalls.d \
./Src/sysmem.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DNUCLEO_C031C6 -DSTM32 -DSTM32C0 -DSTM32C031C6Tx -c -I../Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Src/%.o: ../Src/%.s Src/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m0plus -g3 -DDEBUG -c -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@" "$<"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/clock.cyclo ./Src/clock.d ./Src/clock.o ./Src/clock.su ./Src/context_switch.d ./Src/context_switch.o ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/rtos.cyclo ./Src/rtos.d ./Src/rtos.o ./Src/rtos.su ./Src/scheduler.cyclo ./Src/scheduler.d ./Src/scheduler.o ./Src/scheduler.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su

.PHONY: clean-Src

