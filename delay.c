#include "FreeRTOS.h"
#include "delay.h"
#include "task.h"

/*
 * Busy-wait for `xWcetTicks` ticks.
 */
void vDelayRoutine(TickType_t xWcetTicks)
{
    TickType_t xLastTickCount = xTaskGetTickCount();
    TickType_t xExecutedTicks = 0;
    TickType_t xCurrentTickCount;

    while (xExecutedTicks < xWcetTicks)
    {

        xCurrentTickCount = xTaskGetTickCount();
        if(xLastTickCount != xCurrentTickCount){
            xExecutedTicks++;
            xLastTickCount = xCurrentTickCount;
        }
    }
}
