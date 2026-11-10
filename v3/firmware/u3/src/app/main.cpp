/* main.cpp: U3 application entry (started by the boot manager at U3_APP_BASE). */
#include "hw.h"
#include "app.h"
#include "stm32g0xx.h"

int main()
{
    hw_init();
    hw_iwdg_start();
    app_init();
    for (;;) {
        hw_iwdg_refresh();
        app_loop();
        // Sleep until the next interrupt: SysTick (1 ms), a UART byte or a sensor edge. KNX bytes and ACK
        // decisions are handled in the USART1 interrupt anyway; sleeping roughly halves U3's bus current.
        __WFI();
    }
}
