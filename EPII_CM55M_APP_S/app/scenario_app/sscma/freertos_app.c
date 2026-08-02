#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "WE2_device.h"
#ifdef FREERTOS
/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#endif
/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 * used by the Idle task. */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
		StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
	/* If the buffers to be provided to the Idle task are declared inside this
	 * function then they must be declared static - otherwise they will be allocated on
	 * the stack and so not exists after this function exits. */
	static StaticTask_t xIdleTaskTCB;
	static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE + 100];

	/* Pass out a pointer to the StaticTask_t structure in which the Idle
	 * task's state will be stored. */
	*ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

	/* Pass out the array that will be used as the Idle task's stack. */
	*ppxIdleTaskStackBuffer = uxIdleTaskStack;

	/* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
	 * Note that, as the array is necessarily of type StackType_t,
	 * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE + 100;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
 * application must provide an implementation of vApplicationGetTimerTaskMemory()
 * to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
		StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
	/* If the buffers to be provided to the Timer task are declared inside this
	 * function then they must be declared static - otherwise they will be allocated on
	 * the stack and so not exists after this function exits. */
	static StaticTask_t xTimerTaskTCB;
	static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

	/* Pass out a pointer to the StaticTask_t structure in which the Timer
	 * task's state will be stored. */
	*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

	/* Pass out the array that will be used as the Timer task's stack. */
	*ppxTimerTaskStackBuffer = uxTimerTaskStack;

	/* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
	 * Note that, as the array is necessarily of type StackType_t,
	 * configTIMER_TASK_STACK_DEPTH is specified in words, not bytes. */
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
/*-----------------------------------------------------------*/


/* ---------------------------------------------------------------------------
 * Failure reporting.
 *
 * Every one of these paths used to fail SILENTLY: configASSERT() spun forever
 * with interrupts disabled, and the stack-overflow / malloc-failed hooks were
 * compiled out entirely (configCHECK_FOR_STACK_OVERFLOW and
 * configUSE_MALLOC_FAILED_HOOK were both 0). A tripped assert, a blown task
 * stack and a genuine lockup all looked identical from the host: the device
 * simply stopped talking. These hooks make each one say which it was.
 *
 * They print with xprintf/printf, which retargets to console_putchar() — a
 * polled, byte-at-a-time write to UART0. That matters here: it still works with
 * interrupts masked and with the TX DMA dead, which is exactly the state these
 * hooks run in.
 * ------------------------------------------------------------------------- */

void vAssertCalled(unsigned long ulPC, unsigned long ulLine) {
	taskDISABLE_INTERRUPTS();
	/* ulPC is the return address of the function containing the failed
	 * configASSERT(). Resolve it with:
	 *   arm-none-eabi-addr2line -f -e <...>_s.elf <ulPC> */
	printf("\r\n!! FreeRTOS configASSERT FAILED  pc=0x%08lX line=%lu\r\n", ulPC, ulLine);
	for (;;) {
	}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
	(void) xTask;
	taskDISABLE_INTERRUPTS();
	printf("\r\n!! STACK OVERFLOW in task '%s'\r\n", pcTaskName ? pcTaskName : "?");
	for (;;) {
	}
}

void vApplicationMallocFailedHook(void) {
	/* pvPortMalloc() returned NULL. Callers here mostly do not check, so this
	 * would otherwise surface later as a null-pointer HardFault far from the
	 * real cause — or as silent corruption. */
	taskDISABLE_INTERRUPTS();
	printf("\r\n!! pvPortMalloc FAILED  free=%u min_ever=%u\r\n",
			(unsigned) xPortGetFreeHeapSize(),
			(unsigned) xPortGetMinimumEverFreeHeapSize());
	for (;;) {
	}
}

/*-----------------------------------------------------------*/

void prvGetRegistersFromStack(uint32_t *pulFaultStackAddress) {
	/* These are volatile to try and prevent the compiler/linker optimising them
	 * away as the variables never actually get used.  If the debugger won't show the
	 * values of the variables, make them global my moving their declaration outside
	 * of this function. */
	volatile uint32_t r0;
	volatile uint32_t r1;
	volatile uint32_t r2;
	volatile uint32_t r3;
	volatile uint32_t r12;
	volatile uint32_t lr; /* Link register. */
	volatile uint32_t pc; /* Program counter. */
	volatile uint32_t psr; /* Program status register. */

	r0 = pulFaultStackAddress[0];
	r1 = pulFaultStackAddress[1];
	r2 = pulFaultStackAddress[2];
	r3 = pulFaultStackAddress[3];

	r12 = pulFaultStackAddress[4];
	lr = pulFaultStackAddress[5];
	pc = pulFaultStackAddress[6];
	psr = pulFaultStackAddress[7];

	/* Remove compiler warnings about the variables not being used. */
	(void) r0;
	(void) r1;
	(void) r2;
	(void) r3;
	(void) r12;
	(void) lr; /* Link register. */
	(void) pc; /* Program counter. */
	(void) psr; /* Program status register. */

	/* When the following line is hit, the variables contain the register values. */
	for (;;) {
	}
}
/*-----------------------------------------------------------*/

#if defined(__GNUC)
/**
 * @brief The fault handler implementation calls a function called
 * prvGetRegistersFromStack().
 */
void MemManage_Handler(void)
{
    __asm volatile(
        " tst lr, #4                                                \n"
        " ite eq                                                    \n"
        " mrseq r0, msp                                             \n"
        " mrsne r0, psp                                             \n"
        " ldr r1, handler2_address_const                            \n"
        " bx r1                                                     \n"
        "                                                           \n"
        " handler2_address_const: .word prvGetRegistersFromStack    \n");
}
/*-----------------------------------------------------------*/
#endif
