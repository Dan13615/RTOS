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
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define UNUSED(x) (void)(x)

QueueHandle_t xQueueSimpleVar = NULL;
QueueHandle_t xQueueArray = NULL;
QueueHandle_t xQueueString = NULL;
QueueHandle_t xQueueTimeout = NULL;

typedef int SimpleVar_t;

typedef struct {
    int data[10];
    int length;
} ArrayData_t;

typedef struct {
    char str[50];
} StringData_t;

typedef struct {
    int id;
    char message[30];
} TimeoutTest_t;

// TASK 1
static void vProducerTask(void *pvParameters)
{
    UNUSED(pvParameters);
    int counter = 0;
    SimpleVar_t var;
    ArrayData_t array;
    StringData_t value;
    const TickType_t xDelay100ms = 100 / portTICK_PERIOD_MS;

    printf("\nTask1 started\n");

    for(;;) {
        // sending a simple variable
        var = counter++;
        if(xQueueSend(xQueueSimpleVar, &var, portMAX_DELAY) == pdTRUE)
            printf("Task1 Sent simple var: %d\n", var);
        else
            printf("Task1 FAILED to send simple var\n");
        // sending an array
        vTaskDelay(xDelay100ms);

        array.length = 5;
        for(int i = 0; i < array.length; i++)
            array.data[i] = (counter * 10) + i;

        if(xQueueSend(xQueueArray, &array, portMAX_DELAY) == pdTRUE)
            printf("Task1 Sent array with %d elements\n", array.length);
        else
            printf("Task1 FAILED to send array\n");

        vTaskDelay(xDelay100ms);

        // sending a string
        snprintf(value.str, sizeof(value.str), "MSG #%d", counter);
        if(xQueueSend(xQueueString, &value, portMAX_DELAY) == pdTRUE)
            printf("Task1 Sent string: %s\n", value.str);
        else
            printf("Task1 FAILED to send string\n");

        vTaskDelay(xDelay100ms * 10);  // wait next cycle
    }
}

// TASK 2
static void vReceiverBlockingTask(void *pvParameters)
{
    UNUSED(pvParameters);
    SimpleVar_t receivedSimple;
    ArrayData_t receivedArray;
    StringData_t receivedString;
    const TickType_t xBlockTime = 500 / portTICK_PERIOD_MS;  // 500ms timeout
    BaseType_t xStatus;

    printf("\nTask 2 started\n");

    for(;;) {
        // sending a simple variable with timeout of 500ms
        xStatus = xQueueReceive(xQueueSimpleVar, &receivedSimple, xBlockTime);
        if(xStatus == pdTRUE) {
            printf("Task 2 Received simple var: %d\n", receivedSimple);
        } else {
            printf("Task 2 Timeout receiving simple var\n");
        }
        
        /* Receive array with timeout */
        xStatus = xQueueReceive(xQueueArray, &receivedArray, xBlockTime);
        if(xStatus == pdTRUE) {
            printf("Task 2 Received array: [");
            for(int i = 0; i < receivedArray.length; i++) {
                printf("%d%s", receivedArray.data[i], 
                       i < receivedArray.length - 1 ? ", " : "");
            }
            printf("]\n");
        } else {
            printf("Task 2 Timeout receiving array\n");
        }
        
        /* Receive string with timeout */
        xStatus = xQueueReceive(xQueueString, &receivedString, xBlockTime);
        if(xStatus == pdTRUE) {
            printf("Task 2 Received string: %s\n", receivedString.str);
        } else {
            printf("Task 2 Timeout receiving string\n");
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// TASK 3
static void vReceiverNonBlockingTask(void *pvParameters)
{
    UNUSED(pvParameters);
    SimpleVar_t receivedSimple;
    ArrayData_t receivedArray;
    StringData_t receivedString;
    BaseType_t xStatus;
    static int pollCount = 0;
    
    printf("\nTask 3 started\n");
    
    for(;;) {
        
        pollCount++;
        
        /* Non-blocking receive - simple variable */
        xStatus = xQueueReceive(xQueueSimpleVar, &receivedSimple, 0);
        if(xStatus == pdTRUE) {
            printf("Task 3 Got simple var: %d\n", receivedSimple);
        } else if(pollCount % 20 == 0) {  /* Print status every 20 polls */
            printf("Task3 Poll %d - queue empty\n", pollCount);
        }
        
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        /* Non-blocking receive - array */
        xStatus = xQueueReceive(xQueueArray, &receivedArray, 0);
        if(xStatus == pdTRUE) {
            printf("Task 3 Got array with %d elements\n", receivedArray.length);
        }
        
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        /* Non-blocking receive - string */
        xStatus = xQueueReceive(xQueueString, &receivedString, 0);
        if(xStatus == pdTRUE) {
            printf("Task 3 Got string: %s\n", receivedString.str);
        }
        
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// Task 4
/* Tests scenarios where send timeout vs receive timeout differ */
static void vTimeoutTestTask(void *pvParameters)
{
    UNUSED(pvParameters);
    TimeoutTest_t testData;
    BaseType_t xStatus;
    TickType_t xSendTimeout;
    TickType_t xReceiveTimeout;
    int testPhase = 0;
    const TickType_t xDelay = 1000 / portTICK_PERIOD_MS;

    printf("\nTask 4 started testing send/receive timeout relations\n");

    for(;;) {
        testPhase++;
        printf("\n--- Test Phase %d ---\n", testPhase);
        // sending timeout 200 ms < receiving timeout 500ms
        if(testPhase % 3 == 1) {
            xSendTimeout = 200 / portTICK_PERIOD_MS;
            xReceiveTimeout = 500 / portTICK_PERIOD_MS;

            testData.id = testPhase;
            snprintf(testData.message, sizeof(testData.message), "Send < Receive");

            printf("Task 4: Send timeout=%ums < Recv timeout=%ums\n", 
                   xSendTimeout * portTICK_PERIOD_MS, 
                   xReceiveTimeout * portTICK_PERIOD_MS);

            xStatus = xQueueSend(xQueueTimeout, &testData, xSendTimeout);
            printf("Task 4 Send result: %s\n", 
                   xStatus == pdTRUE ? "SUCCESS (data sent)" : "FAIL (timeout)");
        }

        // Sending timeout 800ms > receiving timeout 200ms
        else if(testPhase % 3 == 2) {
            xSendTimeout = 800 / portTICK_PERIOD_MS;
            xReceiveTimeout = 200 / portTICK_PERIOD_MS;

            testData.id = testPhase;
            snprintf(testData.message, sizeof(testData.message), "Send > Receive");

            printf("Task 4: Send timeout=%ums > Recv timeout=%ums\n", 
                   xSendTimeout * portTICK_PERIOD_MS, 
                   xReceiveTimeout * portTICK_PERIOD_MS);

            xStatus = xQueueSend(xQueueTimeout, &testData, xSendTimeout);
            printf("Task 4 Send result: %s\n", 
                   xStatus == pdTRUE ? "SUCCESS (data sent)" : "FAIL (timeout)");
        }

        // receive with timeout
        else {
            xReceiveTimeout = 300 / portTICK_PERIOD_MS;
            printf("Task 4: Phase 3: Attempting receive with %ums timeout\n", 
                   xReceiveTimeout * portTICK_PERIOD_MS);

            xStatus = xQueueReceive(xQueueTimeout, &testData, xReceiveTimeout);
            if(xStatus == pdTRUE) {
                printf("Task 4 Receive SUCCESS: ID=%d, Msg='%s'\n", 
                       testData.id, testData.message);
            } else {
                printf("Task 4 Receive TIMEOUT - queue was empty\n");
            }
        }

        vTaskDelay(xDelay);
    }
}

// TASK 5
static void vStatisticsTask(void *pvParameters)
{
    UNUSED(pvParameters);
    const TickType_t xDelay = 2000 / portTICK_PERIOD_MS;
    UBaseType_t uxQueueMessages;
    
    printf("\nTask 5 started (prints every 2 seconds)\n");
    
    vTaskDelay(xDelay);
    
    for(;;) {
        printf("\nQUEUE STATISTICS \n");
        uxQueueMessages = uxQueueMessagesWaiting(xQueueSimpleVar);
        printf("SimpleVar Queue: %lu message(s)\n", uxQueueMessages);
        uxQueueMessages = uxQueueMessagesWaiting(xQueueArray);
        printf("Array Queue:     %lu message(s)\n", uxQueueMessages);
        uxQueueMessages = uxQueueMessagesWaiting(xQueueString);
        printf("String Queue:    %lu message(s)\n", uxQueueMessages);
        uxQueueMessages = uxQueueMessagesWaiting(xQueueTimeout);
        printf("Timeout Queue:   %lu message(s)\n", uxQueueMessages);
        vTaskDelay(xDelay);
    }
}

int main(void)
{
    /* Create queues */
    /* Queue for simple integers - 10 items */
    xQueueSimpleVar = xQueueCreate(10, sizeof(SimpleVar_t));
    if(xQueueSimpleVar == NULL) {
        printf("[MAIN] ERROR: Failed to create SimpleVar queue\n");
        exit(-1);
    }
    printf("[MAIN] Created SimpleVar queue (10 items)\n");
    
    /* Queue for arrays - 5 items */
    xQueueArray = xQueueCreate(5, sizeof(ArrayData_t));
    if(xQueueArray == NULL) {
        printf("[MAIN] ERROR: Failed to create Array queue\n");
        exit(-1);
    }
    printf("[MAIN] Created Array queue (5 items)\n");
    
    /* Queue for strings - 8 items */
    xQueueString = xQueueCreate(8, sizeof(StringData_t));
    if(xQueueString == NULL) {
        printf("[MAIN] ERROR: Failed to create String queue\n");
        exit(-1);
    }
    printf("[MAIN] Created String queue (8 items)\n");
    
    /* Queue for timeout testing - 3 items */
    xQueueTimeout = xQueueCreate(3, sizeof(TimeoutTest_t));
    if(xQueueTimeout == NULL) {
        printf("[MAIN] ERROR: Failed to create Timeout queue\n");
        exit(-1);
    }
    printf("[MAIN] Created Timeout queue (3 items)\n");
    
    /* Create tasks */
    printf("\n[MAIN] Creating tasks...\n");
    
    xTaskCreate(vProducerTask, 
                "Producer", 
                configMINIMAL_STACK_SIZE * 2, 
                NULL, 
                2, 
                NULL);
    printf("[MAIN] Created Producer task\n");
    
    xTaskCreate(vReceiverBlockingTask, 
                "RX_Blocking", 
                configMINIMAL_STACK_SIZE * 2, 
                NULL, 
                1, 
                NULL);
    printf("[MAIN] Created Blocking Receiver task\n");
    
    xTaskCreate(vReceiverNonBlockingTask, 
                "RX_NonBlock", 
                configMINIMAL_STACK_SIZE * 2, 
                NULL, 
                1, 
                NULL);
    printf("[MAIN] Created Non-Blocking Receiver task\n");
    
    xTaskCreate(vTimeoutTestTask, 
                "TimeoutTest", 
                configMINIMAL_STACK_SIZE * 2, 
                NULL, 
                1, 
                NULL);
    printf("[MAIN] Created Timeout Test task\n");
    
    xTaskCreate(vStatisticsTask, 
                "Statistics", 
                configMINIMAL_STACK_SIZE, 
                NULL, 
                1, 
                NULL);
    printf("[MAIN] Created Statistics task\n");
    
    printf("\n[MAIN] Starting scheduler...\n\n");
    
    /* Start the scheduler */
    vTaskStartScheduler();
    
    /* Should never reach here */
    printf("[MAIN] ERROR: Scheduler failed\n");
    return -1;
}
