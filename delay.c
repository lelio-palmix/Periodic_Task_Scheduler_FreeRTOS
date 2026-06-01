#include "FreeRTOS.h"
#include "delay.h"
#include "task.h"

/*
 * Busy-wait for `wcetTicks` ticks.
 */
void delay_routine(TickType_t wcetTicks)
{
    TickType_t xLastTickCount = xTaskGetTickCount();
    TickType_t xExecutedTicks = 0;
    TickType_t xCurrentTickCount;
    
    while (xExecutedTicks <= wcetTicks)
    {

        xCurrentTickCount = xTaskGetTickCount(); 
        if(xLastTickCount != xCurrentTickCount){
            xExecutedTicks++;
            xLastTickCount = xCurrentTickCount;
        }
    }
}