/*
    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>>> AND MODIFIED BY <<<< the FreeRTOS exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<
    ***************************************************************************

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available on the following
    link: http://www.freertos.org/a00114.html

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that is more than just the market leader, it     *
     *    is the industry's de facto standard.                               *
     *                                                                       *
     *    Help yourself get started quickly while simultaneously helping     *
     *    to support the FreeRTOS project by purchasing a FreeRTOS           *
     *    tutorial book, reference manual, or both:                          *
     *    http://www.FreeRTOS.org/Documentation                              *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
    the FAQ page "My application does not run, what could be wrong?".  Have you
    defined configASSERT()?

    http://www.FreeRTOS.org/support - In return for receiving this top quality
    embedded software for free we request you assist our global community by
    participating in the support forum.

    http://www.FreeRTOS.org/training - Investing in training allows your team to
    be as productive as possible as early as possible.  Now you can receive
    FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
    Ltd, and the world's leading authority on the world's leading RTOS.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

    http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and commercial middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/

/*
 * main() creates all the demo application tasks, then starts the scheduler.
 * The web documentation provides more details of the standard demo application
 * tasks, which provide no particular functionality but do provide a good
 * example of how to use the FreeRTOS API.
 *
 * In addition to the standard demo tasks, the following tasks and tests are
 * defined and/or created within this file:
 *
 * "Check" task - This only executes every five seconds but has a high priority
 * to ensure it gets processor time.  Its main function is to check that all the
 * standard demo tasks are still operational.  While no errors have been
 * discovered the check task will print out "OK" and the current simulated tick
 * time.  If an error is discovered in the execution of a task then the check
 * task will print out an appropriate error message.
 *
 */


/* Standard includes. */
#include <stdio.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Some definitions */
#define mainQUEUE_LENGTH				( 1 )

// time based define
#define SEND_DELAY				( 2000 ) // 2000
#define RECEIVE_DELAY				( 500 )

#define SEND_TIMEOUT    pdMS_TO_TICKS( 1000 ) // 1000
#define RECEIVE_TIMEOUT pdMS_TO_TICKS( 500 )


#define mainARRAY_LENGTH				( 5 )
#define mainSTRING_LENGTH				( 50 )



/*-----------------------------------------------------------*/
static void xStartTask_1( void *pvParameters )
{
	QueueHandle_t *xQueues = ( QueueHandle_t * ) pvParameters;
	int xInteger = 0;
	int xArray[ mainARRAY_LENGTH ];
	char pcString[ mainSTRING_LENGTH ];
	int xIndex;
	const TickType_t xDelay = pdMS_TO_TICKS( SEND_DELAY );

	for( ;; ) {
		for( xIndex = 0; xIndex < mainARRAY_LENGTH; xIndex++ ) {
			xArray[ xIndex ] = xInteger + xIndex;
		}

		snprintf( pcString, mainSTRING_LENGTH,
				  "this is a string number %d", xInteger );

		vTaskDelay( xDelay );

		// maximum delay, wait infinite time until has space.
		// xQueueSend( xQueues[ 0 ], &xInteger, portMAX_DELAY );
		// xQueueSend( xQueues[ 1 ], xArray, portMAX_DELAY );
		// xQueueSend( xQueues[ 2 ], pcString, portMAX_DELAY );

		// now timeout a certain amount of time, if the queue is full, then skip sending.
		if( xQueueSend( xQueues[ 0 ], &xInteger, SEND_TIMEOUT ) != pdPASS ) {
			printf( "Send timeout INTEGER\r\n" );
		}
		if (xQueueSend( xQueues[ 1 ], xArray, SEND_TIMEOUT ) != pdPASS ) {
			printf( "Send timeout ARRAY\r\n" );
		}
		if (xQueueSend( xQueues[ 2 ], pcString, SEND_TIMEOUT ) != pdPASS ) {
			printf( "Send timeout STRING\r\n" );
		}

		xInteger++;
	}
}


/*-----------------------------------------------------------*/
static void xStartTask_2( void *pvParameters )
{
	QueueHandle_t *xQueues = ( QueueHandle_t * ) pvParameters;
	int xInteger;
	int xArray[ mainARRAY_LENGTH ];
	char pcString[ mainSTRING_LENGTH ];
	int xIndex;
	const TickType_t xDelay = pdMS_TO_TICKS( RECEIVE_DELAY );

	for( ;; ) {

		// 0 timeout mean return imediate, RECEIVE_TIMEOUT mean wait a certain amount of time, if the queue is empty +time is passed, then skip receiving.
		//if( xQueueReceive( xQueues[ 1 ], xArray, 0 ) == pdPASS )
		if( xQueueReceive( xQueues[ 0 ], &xInteger, RECEIVE_TIMEOUT) == pdPASS ) {
			printf( "int: %d\r\n", xInteger );
		} else {
			printf( "int queue empty\r\n" );
		}

		//if( xQueueReceive( xQueues[ 1 ], xArray, 0 ) == pdPASS )
		if( xQueueReceive( xQueues[ 1 ], xArray, RECEIVE_TIMEOUT) == pdPASS ) {
			printf( "Array:" );
			for( xIndex = 0; xIndex < mainARRAY_LENGTH; xIndex++ ) {
				printf( " %d", xArray[ xIndex ] );
			}
			printf( "\r\n" );
		} else {
			printf( "arr queue empty\r\n" );
		}

		// if( xQueueReceive( xQueues[ 2 ], pcString, 0 ) == pdPASS
		if( xQueueReceive( xQueues[ 2 ], pcString, RECEIVE_TIMEOUT ) == pdPASS ){
			printf( "string: %s\r\n", pcString );
		} else {
			printf( "string queue empty\r\n" );
		}

		printf( "\r\n" );
		fflush( stdout );
		vTaskDelay( xDelay );
	}
}

/*-----------------------------------------------------------*/

int main ( void )
{
	QueueHandle_t xQueues[ 3 ];

	xQueues[ 0 ] = xQueueCreate( mainQUEUE_LENGTH, sizeof( int ) );
	xQueues[ 1 ] = xQueueCreate( mainQUEUE_LENGTH,
								sizeof( int ) * mainARRAY_LENGTH );
	xQueues[ 2 ] = xQueueCreate( mainQUEUE_LENGTH,
								mainSTRING_LENGTH * sizeof( char ) );

	/* Create the tasks defined within this file. */
	xTaskCreate( xStartTask_1, "Task_1", configMINIMAL_STACK_SIZE,
				 xQueues, 1, NULL );
	xTaskCreate( xStartTask_2, "Task_2", configMINIMAL_STACK_SIZE,
				 xQueues, 1, NULL );

	/* Start the scheduler itself. */
	vTaskStartScheduler();

	/* Should never get here unless there was not enough heap space to create
	the idle and other system tasks. */
	return 0;
}
/*-----------------------------------------------------------*/
