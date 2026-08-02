#include "app_watchdog.h"
#include "app_watchdog_evidence.h"

static IWDG_HandleTypeDef *app_iwdg;

#if APP_WATCHDOG_TEST_SUPPORT_ENABLED
volatile App_Watchdog_Diagnostics_t g_appWatchdogDiagnostics;
#else
static volatile App_Watchdog_Diagnostics_t g_appWatchdogDiagnostics;
#endif

static void App_Watchdog_RecordHeartbeat(
    uint32_t now,
    volatile uint32_t *count,
    volatile uint32_t *last_tick,
    volatile uint32_t *max_interval_ms)
{
    if (*count != 0U)
    {
        uint32_t interval_ms = (uint32_t)(now - *last_tick);

        if (interval_ms > *max_interval_ms)
        {
            *max_interval_ms = interval_ms;
        }
    }

    (*count)++;
    *last_tick = now;
}

HAL_StatusTypeDef App_Watchdog_Init(IWDG_HandleTypeDef *watchdog)
{
    if ((watchdog == (IWDG_HandleTypeDef *)0) ||
        (watchdog->Instance != IWDG1) ||
        (watchdog->Init.Prescaler != IWDG_PRESCALER_128) ||
        (watchdog->Init.Window != IWDG_WINDOW_DISABLE) ||
        (watchdog->Init.Reload != APP_WATCHDOG_RELOAD_VALUE))
    {
        g_appWatchdogDiagnostics.init_error_count++;
        return HAL_ERROR;
    }

    app_iwdg = watchdog;

    g_appWatchdogDiagnostics.initialized = 1U;
#if APP_WATCHDOG_TEST_SUPPORT_ENABLED
    g_appWatchdogDiagnostics.test_inhibit_refresh = 0U;
    g_appWatchdogDiagnostics.test_suppress_heartbeat_mask = 0U;
#endif
    g_appWatchdogDiagnostics.required_heartbeat_mask =
        APP_WATCHDOG_REQUIRED_HEARTBEATS;
    g_appWatchdogDiagnostics.reported_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.observed_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.last_missing_heartbeat_mask = 0U;
    g_appWatchdogDiagnostics.init_count++;
    g_appWatchdogDiagnostics.last_refresh_tick = HAL_GetTick();
    g_appWatchdogDiagnostics.last_health_check_tick =
        g_appWatchdogDiagnostics.last_refresh_tick;

    App_Watchdog_Evidence_Record(
        APP_WATCHDOG_EVIDENCE_HARD_STALL,
        APP_WATCHDOG_REQUIRED_HEARTBEATS,
        0U,
        0U,
        g_appWatchdogDiagnostics.last_health_check_tick,
        g_appWatchdogDiagnostics.last_refresh_tick);

    return HAL_OK;
}

void App_Watchdog_CheckIn(App_Watchdog_Heartbeat_t heartbeat)
{
    uint32_t heartbeat_mask = (uint32_t)heartbeat;
    uint32_t now = HAL_GetTick();

    if (g_appWatchdogDiagnostics.initialized == 0U)
    {
        return;
    }

    heartbeat_mask &= APP_WATCHDOG_REQUIRED_HEARTBEATS;
    g_appWatchdogDiagnostics.reported_heartbeat_mask |= heartbeat_mask;

    if ((heartbeat_mask & APP_WATCHDOG_HEARTBEAT_MAIN_LOOP) != 0U)
    {
        App_Watchdog_RecordHeartbeat(
            now,
            &g_appWatchdogDiagnostics.main_loop_heartbeat_count,
            &g_appWatchdogDiagnostics.main_loop_last_heartbeat_tick,
            &g_appWatchdogDiagnostics.main_loop_max_heartbeat_interval_ms);
    }

    if ((heartbeat_mask & APP_WATCHDOG_HEARTBEAT_CAN_APP) != 0U)
    {
        App_Watchdog_RecordHeartbeat(
            now,
            &g_appWatchdogDiagnostics.can_app_heartbeat_count,
            &g_appWatchdogDiagnostics.can_app_last_heartbeat_tick,
            &g_appWatchdogDiagnostics.can_app_max_heartbeat_interval_ms);
    }

    if ((heartbeat_mask & APP_WATCHDOG_HEARTBEAT_RTC_SERVICE) != 0U)
    {
        App_Watchdog_RecordHeartbeat(
            now,
            &g_appWatchdogDiagnostics.rtc_service_heartbeat_count,
            &g_appWatchdogDiagnostics.rtc_service_last_heartbeat_tick,
            &g_appWatchdogDiagnostics.rtc_service_max_heartbeat_interval_ms);
    }

#if APP_WATCHDOG_TEST_SUPPORT_ENABLED
    heartbeat_mask &=
        ~g_appWatchdogDiagnostics.test_suppress_heartbeat_mask;
#endif
    g_appWatchdogDiagnostics.observed_heartbeat_mask |= heartbeat_mask;
}

void App_Watchdog_Process(void)
{
    uint32_t now;
    uint32_t observed_mask;
    uint32_t missing_mask;

    if (g_appWatchdogDiagnostics.initialized == 0U)
    {
        return;
    }

#if APP_WATCHDOG_TEST_SUPPORT_ENABLED
    if (g_appWatchdogDiagnostics.test_inhibit_refresh != 0U)
    {
        return;
    }
#endif

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
        App_Watchdog_Evidence_Record(
            APP_WATCHDOG_EVIDENCE_HEALTH_GATE_REJECTED,
            APP_WATCHDOG_REQUIRED_HEARTBEATS,
            observed_mask,
            missing_mask,
            now,
            g_appWatchdogDiagnostics.last_refresh_tick);
        return;
    }

    if (HAL_IWDG_Refresh(app_iwdg) == HAL_OK)
    {
        g_appWatchdogDiagnostics.refresh_count++;
        g_appWatchdogDiagnostics.last_refresh_tick = now;
        App_Watchdog_Evidence_Record(
            APP_WATCHDOG_EVIDENCE_HARD_STALL,
            APP_WATCHDOG_REQUIRED_HEARTBEATS,
            observed_mask,
            0U,
            now,
            now);
    }
    else
    {
        g_appWatchdogDiagnostics.refresh_error_count++;
        App_Watchdog_Evidence_Record(
            APP_WATCHDOG_EVIDENCE_REFRESH_ERROR,
            APP_WATCHDOG_REQUIRED_HEARTBEATS,
            observed_mask,
            0U,
            now,
            g_appWatchdogDiagnostics.last_refresh_tick);
    }
}

#if APP_WATCHDOG_TEST_SUPPORT_ENABLED
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
#endif
