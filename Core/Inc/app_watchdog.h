#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include "main.h"
#include "stm32h7xx_hal_iwdg.h"

#include <stdint.h>

#if !defined(HAL_IWDG_MODULE_ENABLED)
#error "Enable HAL_IWDG_MODULE_ENABLED in stm32h7xx_hal_conf.h"
#endif

/*
 * Nominal timeout calculation:
 *   (RELOAD + 1) * PRESCALER / LSI = 1000 * 128 / 32000 = 4 s
 *
 * The LSI oscillator is not precision-trimmed, so the real hardware timeout
 * must be verified on the target across the required operating conditions.
 */
#define APP_WATCHDOG_LSI_NOMINAL_HZ       32000UL
#define APP_WATCHDOG_PRESCALER_DIV        128UL
#define APP_WATCHDOG_RELOAD_VALUE         999UL
#define APP_WATCHDOG_TIMEOUT_NOMINAL_MS   4000UL
#define APP_WATCHDOG_REFRESH_PERIOD_MS    250UL

#if defined(DEBUG) || defined(APP_WATCHDOG_TEST_HOOKS)
#define APP_WATCHDOG_TEST_HOOKS_ENABLED   1U
#else
#define APP_WATCHDOG_TEST_HOOKS_ENABLED   0U
#endif

typedef enum
{
    APP_WATCHDOG_HEARTBEAT_MAIN_LOOP = (1UL << 0),
    APP_WATCHDOG_HEARTBEAT_CAN_APP = (1UL << 1),
    APP_WATCHDOG_HEARTBEAT_RTC_SERVICE = (1UL << 2)
} App_Watchdog_Heartbeat_t;

#define APP_WATCHDOG_REQUIRED_HEARTBEATS                         \
    ((uint32_t)APP_WATCHDOG_HEARTBEAT_MAIN_LOOP |                \
     (uint32_t)APP_WATCHDOG_HEARTBEAT_CAN_APP |                  \
     (uint32_t)APP_WATCHDOG_HEARTBEAT_RTC_SERVICE)

typedef struct
{
    uint32_t init_count;
    uint32_t init_error_count;
    uint32_t refresh_count;
    uint32_t refresh_error_count;
    uint32_t last_refresh_tick;
    uint32_t last_health_check_tick;
    uint32_t required_heartbeat_mask;
    uint32_t reported_heartbeat_mask;
    uint32_t observed_heartbeat_mask;
    uint32_t last_missing_heartbeat_mask;
    uint32_t health_gate_reject_count;
    uint32_t main_loop_heartbeat_count;
    uint32_t can_app_heartbeat_count;
    uint32_t rtc_service_heartbeat_count;
    uint32_t main_loop_last_heartbeat_tick;
    uint32_t can_app_last_heartbeat_tick;
    uint32_t rtc_service_last_heartbeat_tick;
    uint32_t main_loop_max_heartbeat_interval_ms;
    uint32_t can_app_max_heartbeat_interval_ms;
    uint32_t rtc_service_max_heartbeat_interval_ms;
#if APP_WATCHDOG_TEST_HOOKS_ENABLED
    uint32_t test_suppress_heartbeat_mask;
#endif
    uint8_t initialized;
#if APP_WATCHDOG_TEST_HOOKS_ENABLED
    uint8_t test_inhibit_refresh;
#endif
} App_Watchdog_Diagnostics_t;

/* Exposed for debugger inspection. Application code must use the API. */
extern volatile App_Watchdog_Diagnostics_t g_appWatchdogDiagnostics;

HAL_StatusTypeDef App_Watchdog_Init(IWDG_HandleTypeDef *watchdog);
void App_Watchdog_CheckIn(App_Watchdog_Heartbeat_t heartbeat);
void App_Watchdog_Process(void);
#if APP_WATCHDOG_TEST_HOOKS_ENABLED
void App_Watchdog_SetTestInhibit(uint8_t inhibit);
void App_Watchdog_SetTestSuppressHeartbeatMask(uint32_t heartbeat_mask);
#endif

#endif /* APP_WATCHDOG_H */
