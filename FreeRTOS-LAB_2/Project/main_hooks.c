
/* Standard includes. */
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Priorities at which the tasks are created. */
#define mainCHECK_TASK_PRIORITY			( configMAX_PRIORITIES - 2 )
#define mainQUEUE_POLL_PRIORITY			( tskIDLE_PRIORITY + 1 )
#define mainSEM_TEST_PRIORITY			( tskIDLE_PRIORITY + 1 )
#define mainBLOCK_Q_PRIORITY			( tskIDLE_PRIORITY + 2 )
#define mainCREATOR_TASK_PRIORITY		( tskIDLE_PRIORITY + 3 )
#define mainINTEGER_TASK_PRIORITY		( tskIDLE_PRIORITY )
#define mainGEN_QUEUE_TASK_PRIORITY		( tskIDLE_PRIORITY )
#define mainFLOP_TASK_PRIORITY			( tskIDLE_PRIORITY )
#define mainQUEUE_OVERWRITE_PRIORITY	( tskIDLE_PRIORITY )

#define mainTIMER_TEST_PERIOD			( 50 )


/*
 * Prototypes for the standard FreeRTOS callback/hook functions implemented
 * within this file.
 */
void vApplicationMallocFailedHook( void );
void vApplicationIdleHook( void );
void vApplicationTickHook( void );


/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* vApplicationMallocFailedHook() will only be called if
	configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
	function that will get called if a call to pvPortMalloc() fails.
	pvPortMalloc() is called internally by the kernel whenever a task, queue,
	timer or semaphore is created.  It is also called by various parts of the
	demo application.  If heap_1.c or heap_2.c are used, then the size of the
	heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
	FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
	to query the size of free heap space that remains (although it does not
	provide information on how the remaining heap might be fragmented). */
	vAssertCalled( __LINE__, __FILE__ );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
	/* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
	to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
	task.  It is essential that code added to this hook function never attempts
	to block in any way (for example, call xQueueReceive() with a block time
	specified, or call vTaskDelay()).  If the application makes use of the
	vTaskDelete() API function (as this demo application does) then it is also
	important that vApplicationIdleHook() is permitted to return to its calling
	function, because it is the responsibility of the idle task to clean up
	memory allocated by the kernel to any task that has since been deleted. */

		/* Call the idle task processing used by the full demo.  The simple
		blinky demo does not use the idle task hook. */
		//vFullDemoIdleFunction();
}
/*-----------------------------------------------------------*/

void vApplicationTickHook( void )
{
	/* This function will be called by each tick interrupt if
	configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h.  User code can be
	added here, but the tick hook is called from an interrupt context, so
	code must not attempt to block, and only the interrupt safe FreeRTOS API
	functions can be used (those that end in FromISR()). */

	/* Call the periodic timer test, which tests the timer API functions that
	can be called from an ISR. */
//	#if( configUSE_PREEMPTION != 0 )
//	{
		/* Only created when preemption is used. */
//		vTimerPeriodicISRTests();
//	}
//	#endif

	/* Call the periodic queue overwrite from ISR demo. */
//	vQueueOverwritePeriodicISRDemo();

	/* Write to a queue that is in use as part of the queue set demo to
	demonstrate using queue sets from an ISR. */
//	vQueueSetAccessQueueSetFromISR();
//	vQueueSetPollingInterruptAccess();

	/* Exercise event groups from interrupts. */
//	vPeriodicEventGroupsProcessing();

	/* Exercise giving mutexes from an interrupt. */
//	vInterruptSemaphorePeriodicTest();

	/* Exercise using task notifications from an interrupt. */
//	xNotifyTaskFromISR();
}
/*-----------------------------------------------------------*/

void vAssertCalled( unsigned long ulLine, const char * const pcFileName )
{
 	taskENTER_CRITICAL();
	{
        printf("[ASSERT] %s:%lu\n", pcFileName, ulLine);
        fflush(stdout);
	}
	taskEXIT_CRITICAL();
	exit(-1);
}
/*-----------------------------------------------------------*/
