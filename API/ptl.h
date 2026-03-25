#ifndef PTL_H
#define PTL_H

#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "uart.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "delay.h"

#define MAX_WAIT portMAX_DELAY
#define QUEUE_LENGTH 15
#define MESSAGE_LENGTH 60

#define DEFAULT_STACK_SIZE 512

#define POLICY_SKIP 0
#define POLICY_KILL 1
#define POLICY_CATCH_UP 2
#define MAX_TASKS 8
#define TASK_RUNNING 1
#define TASK_NOT_RUNNING 2

/* Define your structs here */
typedef struct
{
    TaskHandle_t task;
    int state;
    int k; //keeps track of job releases
    int policy;
} TaskState;

typedef struct
{
    char *name;
    int idTask;
    void (*taskBody)(void *);
    void *params;
    configSTACK_DEPTH_TYPE stackDepth;
    UBaseType_t uxPriority;
    int period_ms;
    int deadline;
    int offset_ms;
} TaskConfig;

typedef struct
{
    int policy;
    int trace_enabled;
    int max_tasks;
    TaskConfig *tasks;
    int num_tasks;
}  SchedulerConfig;

extern volatile QueueHandle_t logQueue;

extern volatile SemaphoreHandle_t xSemaphore;

extern volatile TaskState taskState[MAX_TASKS];

/* Function declarations */
void Task_Function(void *params);
void LoggingTask(void *params);
void Init( const SchedulerConfig (sconfig));


#endif /* PTL_H */