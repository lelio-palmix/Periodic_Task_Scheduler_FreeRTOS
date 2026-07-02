#include "uart.h"

void vUartInit( void )
{
    UART0_BAUDDIV = 16;
    UART0_CTRL = 1;
}

void vUartPrintf(const char *pcString) {
    while(*pcString != '\0') {
        UART0_DATA = (unsigned int)(*pcString);
        pcString++;
    }
}
