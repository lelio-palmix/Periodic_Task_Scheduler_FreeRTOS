#include "delay.h"

void delay_routine_1(void) {
  asm("mov r1, r0 \n"  // r0 was set in the startup file, move to r1
      "dly: \n"        // label for the loop
      "subs r1, #1 \n" // subtract 1 from r1
      "bne dly \n"     // branch to delay if r1 is not zero
  );                   
}

void delay_routine_2(unsigned int delay_counter) {
  // Delay routine in assembly
  asm("push {r1} \n"
      "mov r1, r0 \n"  // in arm assembly, the first parameter is stored in r0
      "delay: \n"      // label for the loop
          "subs r1, #1 \n" // subtract 1 from r1
          "bne delay \n"   // branch to delay if r1 is not zero
      "pop {r1} \n");  // restore r1
}

