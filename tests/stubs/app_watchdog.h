#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <stdint.h>

typedef enum
{
    APP_WATCHDOG_HEARTBEAT_MAIN_LOOP = (1UL << 0),
    APP_WATCHDOG_HEARTBEAT_CAN_APP = (1UL << 1),
    APP_WATCHDOG_HEARTBEAT_RTC_SERVICE = (1UL << 2)
} App_Watchdog_Heartbeat_t;

void App_Watchdog_CheckIn(App_Watchdog_Heartbeat_t heartbeat);

#endif /* APP_WATCHDOG_H */
