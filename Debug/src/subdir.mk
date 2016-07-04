################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/local_pool.c \
../src/logger.c \
../src/main.c \
../src/page.c \
../src/queue.c \
../src/shared_pool.c \
../src/wfmalloc.c \
../src/large_allocations.c

OBJS += \
./src/local_pool.o \
./src/logger.o \
./src/main.o \
./src/page.o \
./src/queue.o \
./src/shared_pool.o \
./src/wfmalloc.o \
./src/large_allocations.o

C_DEPS += \
./src/local_pool.d \
./src/logger.d \
./src/main.d \
./src/page.d \
./src/queue.d \
./src/shared_pool.d \
./src/wfmalloc.d \
./src/large_allocations.d

CFLAGS = -O3 -DNDEBUG --std=gnu99

CFLAGS_DBG = -g3 -O1 --std=gnu99

# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -DLOGGING_LEVEL=LOG_LEVEL_DEBUG -DDEBUG -DDEBUG_QUEUE -I/lib/modules/3.13.0-74-generic/build/include -O0 -g3 -Wall -c -fmessage-length=0 -std=c11 -D_GNU_SOURCE -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


