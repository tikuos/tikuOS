/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_watchdog.c - Watchdog timer tests
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_watchdog.h"
#include <arch/msp430/tiku_compiler.h>

#ifdef PLATFORM_MSP430

/*---------------------------------------------------------------------------*/
/* PRIVATE CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

#define TEST_WATCHDOG_MAX_KICKS     30
#define TEST_WATCHDOG_KICK_INTERVAL 500  /* milliseconds */
#define TEST_WATCHDOG_DELAY_NORMAL  2500  /* milliseconds */

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                        */
/*---------------------------------------------------------------------------*/
 static volatile unsigned int wdt_kick_count = 0;
 static volatile unsigned int main_loop_count = 0;
 static volatile unsigned int interval_isr_count = 0;
 
 /* Watchdog interval timer ISR (if using interval mode) */
TIKU_ISR(WDT_VECTOR, WDT_ISR)
 {
    interval_isr_count++;

    tiku_common_led1_toggle();
 
}
 
 #if TEST_WDT_BASIC
 /*
  * Test 1: Basic Watchdog Operation
  * - Configure watchdog with ~1 second timeout
  * - Kick watchdog periodically to prevent reset
  * - LED blinks to show system is running
  */
 void test_watchdog_basic(void)
 {
     TEST_PRINTF("Starting basic watchdog test\n");

     TEST_PRINTF("\n=== Test 1: Basic Watchdog Operation ===\n");
     TEST_PRINTF("Watchdog will be kicked every 500ms\n");
     TEST_PRINTF("LED1 will blink to show system is alive\n\n");
     
     /* Configure watchdog: 
      * - Watchdog mode (not interval)
      * - ACLK source (32768 Hz)
      * - ~1 second timeout (32768 cycles)
      */
     tiku_watchdog_config(
         TIKU_WDT_MODE_WATCHDOG,  /* Watchdog mode */
         TIKU_WDT_SRC_ACLK,        /* Use ACLK (32.768 kHz) */
         WDTIS__32768,             /* ~1 second timeout */
         0,                        /* Start immediately */
         1                         /* Kick on start */
     );
     
     TEST_PRINTF("Watchdog configured and started\n");
     TEST_PRINTF("Watchdog started with ~1 second timeout\n");
     
     /* Main loop - kick watchdog periodically */
     while(1) {
         /* Kick the watchdog */
         tiku_watchdog_kick();
         wdt_kick_count++;
         
         /* Toggle LED and print status every 10 kicks */
         if (wdt_kick_count % 10 == 0) {
            tiku_common_led1_toggle();
            TEST_PRINTF("WDT kicked %d times, system alive\n", wdt_kick_count);
         }
         
         /* Delay 500ms (half the watchdog timeout) */
        tiku_common_delay_ms(TEST_WATCHDOG_DELAY_NORMAL);

         /* Stop after 30 kicks for demo */
         if (wdt_kick_count >= 30) {
             TEST_PRINTF("Basic watchdog test completed after %d kicks\n", wdt_kick_count);
             TEST_PRINTF("\nBasic watchdog test completed successfully!\n");
             tiku_watchdog_off();
             break;
         }
     }
 }
 #endif


 #if TEST_WDT_PAUSE_RESUME
 /*
  * Test 2: Watchdog Pause/Resume
  * - Start watchdog
  * - Pause for extended operation
  * - Resume watchdog
  */
 void test_watchdog_pause_resume(void)
 {
     TEST_PRINTF("Starting pause/resume test\n");

     TEST_PRINTF("\n=== Test 2: Watchdog Pause/Resume ===\n");
     
     /* Start watchdog with short timeout */
     #ifdef PLATFORM_MSP430
     tiku_watchdog_config(
         TIKU_WDT_MODE_WATCHDOG,
         TIKU_WDT_SRC_ACLK,
         WDTIS__8192,              /* ~250ms timeout */
         0,
         1
     );
     #else
     tiku_watchdog_init();
     #endif

     TEST_PRINTF("Watchdog started with ~250ms timeout\n");

     /* Run with kicks for a while */
     for (int i = 0; i < 5; i++) {
         tiku_watchdog_kick();
         tiku_common_led1_toggle();
         TEST_PRINTF("Kick %d\n", i + 1);
         tiku_common_delay_ms(100);
     }

     /* Pause watchdog for extended operation */
     TEST_PRINTF("Pausing watchdog for long operation\n");
     TEST_PRINTF("\nPausing watchdog for extended operation...\n");
     tiku_watchdog_pause();

     /* Simulate long operation (would normally cause reset) */
     for (int i = 0; i < 10; i++) {
         tiku_common_led2_toggle();
         TEST_PRINTF("Long operation %d/10 (WDT paused)\n", i + 1);
         tiku_common_delay_ms(300);  /* Longer than WDT timeout */
     }

     /* Resume watchdog with kick */
     TEST_PRINTF("Resuming watchdog operation\n");
     TEST_PRINTF("\nResuming watchdog with kick...\n");
     tiku_watchdog_resume_with_kick();

     /* Continue normal operation */
     for (int i = 0; i < 5; i++) {
         tiku_watchdog_kick();
         tiku_common_led1_toggle();
         TEST_PRINTF("Post-resume kick %d\n", i + 1);
         tiku_common_delay_ms(100);
     }

     TEST_PRINTF("Pause/resume test completed successfully\n");
     TEST_PRINTF("\nPause/Resume test completed successfully!\n");
     tiku_watchdog_off();
 }
 #endif
 
 #if TEST_WDT_INTERVAL
 /*
  * Test 3: Interval Timer Mode
  * - Configure watchdog as interval timer
  * - Generate periodic interrupts
  */
 void test_watchdog_interval_timer(void)
 {
     TEST_PRINTF("Starting interval timer test\n");

     TEST_PRINTF("\n=== Test 3: Interval Timer Mode ===\n");
     TEST_PRINTF("WDT will generate interrupts every ~250ms\n");
     TEST_PRINTF("LED2 will toggle on each interrupt\n\n");
     
  
     /* First, make sure WDT is off */
     tiku_watchdog_off();
     
     /* Configure for interval timer mode manually */
     WDTCTL = WDTPW | WDTHOLD;  /* Stop WDT */
     WDTCTL = WDTPW | WDTTMSEL | WDTCNTCL | WDTSSEL__ACLK | WDTIS__8192;
     
     /* Enable WDT interrupt */
     SFRIE1 |= WDTIE;
     
     TEST_PRINTF("Interval timer configured and started\n");
     TEST_PRINTF("Interval timer started\n");

     /* Main loop - just count and display */
     unsigned int last_isr_count = 0;
     for (int i = 0; i < 50; i++) {
         if (interval_isr_count != last_isr_count) {
             TEST_PRINTF("Interval ISR fired %u times\n", interval_isr_count);
             last_isr_count = interval_isr_count;
         }

         tiku_common_led1_toggle();
         tiku_common_delay_ms(100);
     }

     SFRIE1 &= ~WDTIE;  /* Disable interrupt */
     WDTCTL = WDTPW | WDTHOLD;  /* Stop WDT */

     TEST_PRINTF("Interval timer test completed with %u interrupts\n", interval_isr_count);
     TEST_PRINTF("\nInterval timer test completed!\n");
     TEST_PRINTF("Total interrupts: %u\n", interval_isr_count);
 }
 #endif
 
 #if TEST_WDT_TIMEOUT
 /*
  * Test 4: Watchdog Timeout (System Reset)
  * WARNING: This will reset your device!
  */
 void test_watchdog_timeout(void)
 {
     TEST_PRINTF("Starting timeout test - SYSTEM WILL RESET!\n");

     TEST_PRINTF("\n=== Test 4: Watchdog Timeout Demo ===\n");
     TEST_PRINTF("WARNING: System will reset in 3 seconds!\n");
     TEST_PRINTF("Disconnect debugger if needed.\n\n");
     
     /* Give time to read the message */
     delay_ms(2000);
     

     tiku_watchdog_config(
         TIKU_WDT_MODE_WATCHDOG,
         TIKU_WDT_SRC_ACLK,
         WDTIS__512,               /* ~15ms timeout */
         0,
         1
     );

     
     TEST_PRINTF("Watchdog configured for immediate timeout\n");
     TEST_PRINTF("Watchdog started with ~15ms timeout\n");
     TEST_PRINTF("NOT kicking watchdog - reset imminent!\n");
     TEST_PRINTF("Waiting for system reset...\n");
     
     /* Busy wait without kicking - will cause reset */
     while(1) {
         tiku_common_led1_toggle();
         tiku_common_delay_ms(10);
         /* Deliberately NOT calling tiku_watchdog_kick() */
     }
 }
 #endif


#endif

