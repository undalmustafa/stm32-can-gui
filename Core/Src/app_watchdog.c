#include "app_watchdog.h"

static IWDG_HandleTypeDef app_iwdg;

volatile App_Watchdog_Diagnostics_t g_appWatchdogDiagnostics;

HAL_StatusTypeDef App_Watchdog_Init(void)
{
    HAL_StatusTypeDef status;

#if defined(DEBUG) && defined(__HAL_DBGMCU_FREEZE_IWDG1)
    /* Keep source-level debugging practical. Production builds do not freeze. */
    __HAL_DBGMCU_FREEZE_IWDG1();
#endif

    app_iwdg.Instance = IWDG1;
    app_iwdg.Init.Prescaler = IWDG_PRESCALER_128;
    app_iwdg.Init.Window = IWDG_WINDOW_DISABLE;
    app_iwdg.Init.Reload = APP_WATCHDOG_RELOAD_VALUE;

    status = HAL_IWDG_Init(&app_iwdg);
    if (status != HAL_OK)
    {
        g_appWatchdogDiagnostics.init_error_count++;
        return status;
    }

    g_appWatchdogDiagnostics.initialized = 1U;
    g_appWatchdogDiagnostics.test_inhibit_refresh = 0U;
    g_appWatchdogDiagnostics.test_suppress_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.required_heartbeat_mask =
        APP_WATCHDOG_REQUIRED_HEARTBEATS;
    g_appWatchdogDiagnostics.reported_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.observed_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.last_missing_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.init_count++;
    g_appWatchdogDiagnostics.last_refresh_tick = HAL_GetTick();
    g_appWatchdogDiagnostics.last_health_check_tick =
        g_appWatchdogDiagnostics.last_refresh_tick;

    return HAL_OK;
}

void App_Watchdog_CheckIn(App_Watchdog_Heartbeat_t heartbeat)
{
    uint32_t heartbeat_mask = (uint32_t)heartbeat;
    uint32_t new_heartbeat_mask;
    uint32_t now = HAL_GetTick();

    if (g_appWatchdogDiagnostics.initialized == 0U)
    {
        return;
    }

    heartbeat_mask &= APP_WATCHDOG_REQUIRED_HEARTBEATS;
    new_heartbeat_mask = heartbeat_mask &
        ~g_appWatchdogDiagnostics.reported_heartbeat_mask;
    g_appWatchdogDiagnostics.reported_heartbeat_mask |= heartbeat_mask;

    if ((new_heartbeat_mask & APP_WATCHDOG_HEARTBEAT_MAIN_LOOP) != 0U)
    {
        g_appWatchdogDiagnostics.main_loop_heartbeat_count++;
        g_appWatchdogDiagnostics.main_loop_last_heartbeat_tick = now;
    }

    if ((new_heartbeat_mask & APP_WATCHDOG_HEARTBEAT_CAN_APP) != 0U)
    {
        g_appWatchdogDiagnostics.can_app_heartbeat_count++;
        g_appWatchdogDiagnostics.can_app_last_heartbeat_tick = now;
    }

    if ((new_heartbeat_mask & APP_WATCHDOG_HEARTBEAT_RTC_SERVICE) != 0U)
    {
        g_appWatchdogDiagnostics.rtc_service_heartbeat_count++;
        g_appWatchdogDiagnostics.rtc_service_last_heartbeat_tick = now;
    }

    heartbeat_mask &=
        ~g_appWatchdogDiagnostics.test_suppress_heartbeat_mask;
    g_appWatchdogDiagnostics.observed_heartbeat_mask |= heartbeat_mask;
}

void App_Watchdog_Process(void)
{
    uint32_t now;
    uint32_t observed_mask;
    uint32_t missing_mask;

    if ((g_appWatchdogDiagnostics.initialized == 0U) ||
        (g_appWatchdogDiagnostics.test_inhibit_refresh != 0U))
    {
        return;
    }

    now = HAL_GetTick();
    if ((uint32_t)(now -
                   g_appWatchdogDiagnostics.last_health_check_tick) <
        APP_WATCHDOG_REFRESH_PERIOD_MS)
    {
        return;
    }

    g_appWatchdogDiagnostics.last_health_check_tick = now;
    observed_mask = g_appWatchdogDiagnostics.observed_heartbeat_mask;
    g_appWatchdogDiagnostics.reported_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.observed_heartbeat_mask = 0U;

    missing_mask =
        APP_WATCHDOG_REQUIRED_HEARTBEATS & ~observed_mask;
    g_appWatchdogDiagnostics.last_missing_heartbeat_mask = missing_mask;

    if (missing_mask != 0U)
    {
        g_appWatchdogDiagnostics.health_gate_reject_count++;
        return;
    }

    if (HAL_IWDG_Refresh(&app_iwdg) == HAL_OK)
    {
        g_appWatchdogDiagnostics.refresh_count++;
        g_appWatchdogDiagnostics.last_refresh_tick = now;
    }
    else
    {
        g_appWatchdogDiagnostics.refresh_error_count++;
    }
}

void App_Watchdog_SetTestInhibit(uint8_t inhibit)
{
    g_appWatchdogDiagnostics.test_inhibit_refresh =
        (inhibit != 0U) ? 1U : 0U;
}

void App_Watchdog_SetTestSuppressHeartbeatMask(uint32_t heartbeat_mask)
{
    g_appWatchdogDiagnostics.test_suppress_heartbeat_mask =
        heartbeat_mask & APP_WATCHDOG_REQUIRED_HEARTBEATS;
}
