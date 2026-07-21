#include "watchdog_fake_hal.h"
#include "app_watchdog.h"

#include <stdio.h>

uint32_t g_fakeIwdgInstance;

static uint32_t fake_tick;
static HAL_StatusTypeDef fake_init_status;
static HAL_StatusTypeDef fake_refresh_status;
static uint32_t fake_init_calls;
static uint32_t fake_refresh_calls;
static IWDG_HandleTypeDef fake_initialized_handle;

#define EXPECT(condition)                                                   \
    do                                                                      \
    {                                                                       \
        if (!(condition))                                                   \
        {                                                                   \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            return 1;                                                       \
        }                                                                   \
    } while (0)

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *handle)
{
    fake_init_calls++;
    fake_initialized_handle = *handle;
    return fake_init_status;
}

HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *handle)
{
    (void)handle;
    fake_refresh_calls++;
    return fake_refresh_status;
}

static void ResetFixture(void)
{
    g_appWatchdogDiagnostics = (App_Watchdog_Diagnostics_t){0};
    fake_tick = 0U;
    fake_init_status = HAL_OK;
    fake_refresh_status = HAL_OK;
    fake_init_calls = 0U;
    fake_refresh_calls = 0U;
    fake_initialized_handle = (IWDG_HandleTypeDef){0};
}

static void CheckInAll(void)
{
    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_MAIN_LOOP);
    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_CAN_APP);
    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_RTC_SERVICE);
}

static int TestInitialization(void)
{
    ResetFixture();
    fake_tick = 17U;

    EXPECT(App_Watchdog_Init() == HAL_OK);
    EXPECT(fake_init_calls == 1U);
    EXPECT(fake_initialized_handle.Instance == IWDG1);
    EXPECT(fake_initialized_handle.Init.Prescaler == IWDG_PRESCALER_128);
    EXPECT(fake_initialized_handle.Init.Reload == APP_WATCHDOG_RELOAD_VALUE);
    EXPECT(fake_initialized_handle.Init.Window == IWDG_WINDOW_DISABLE);
    EXPECT(g_appWatchdogDiagnostics.initialized == 1U);
    EXPECT(g_appWatchdogDiagnostics.init_count == 1U);
    EXPECT(g_appWatchdogDiagnostics.last_refresh_tick == 17U);
    EXPECT(g_appWatchdogDiagnostics.required_heartbeat_mask ==
           APP_WATCHDOG_REQUIRED_HEARTBEATS);

    return 0;
}

static int TestInitializationFailure(void)
{
    ResetFixture();
    fake_init_status = HAL_ERROR;

    EXPECT(App_Watchdog_Init() == HAL_ERROR);
    EXPECT(fake_init_calls == 1U);
    EXPECT(g_appWatchdogDiagnostics.initialized == 0U);
    EXPECT(g_appWatchdogDiagnostics.init_error_count == 1U);

    return 0;
}

static int TestHealthyRefreshGate(void)
{
    ResetFixture();
    fake_tick = 10U;
    EXPECT(App_Watchdog_Init() == HAL_OK);

    CheckInAll();
    fake_tick = 259U;
    App_Watchdog_Process();
    EXPECT(fake_refresh_calls == 0U);

    fake_tick = 260U;
    App_Watchdog_Process();
    EXPECT(fake_refresh_calls == 1U);
    EXPECT(g_appWatchdogDiagnostics.refresh_count == 1U);
    EXPECT(g_appWatchdogDiagnostics.last_missing_heartbeat_mask == 0U);
    EXPECT(g_appWatchdogDiagnostics.observed_heartbeat_mask == 0U);

    return 0;
}

static int TestMissingHeartbeatAndRecovery(void)
{
    ResetFixture();
    EXPECT(App_Watchdog_Init() == HAL_OK);

    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_MAIN_LOOP);
    App_Watchdog_CheckIn(APP_WATCHDOG_HEARTBEAT_CAN_APP);
    fake_tick = APP_WATCHDOG_REFRESH_PERIOD_MS;
    App_Watchdog_Process();

    EXPECT(fake_refresh_calls == 0U);
    EXPECT(g_appWatchdogDiagnostics.health_gate_reject_count == 1U);
    EXPECT(g_appWatchdogDiagnostics.last_missing_heartbeat_mask ==
           APP_WATCHDOG_HEARTBEAT_RTC_SERVICE);

    CheckInAll();
    fake_tick += APP_WATCHDOG_REFRESH_PERIOD_MS;
    App_Watchdog_Process();
    EXPECT(fake_refresh_calls == 1U);
    EXPECT(g_appWatchdogDiagnostics.refresh_count == 1U);
    EXPECT(g_appWatchdogDiagnostics.last_missing_heartbeat_mask == 0U);

    return 0;
}

static int TestFaultInjectionControls(void)
{
    ResetFixture();
    EXPECT(App_Watchdog_Init() == HAL_OK);

    App_Watchdog_SetTestSuppressHeartbeatMask(
        APP_WATCHDOG_HEARTBEAT_RTC_SERVICE);
    CheckInAll();
    fake_tick = APP_WATCHDOG_REFRESH_PERIOD_MS;
    App_Watchdog_Process();
    EXPECT(fake_refresh_calls == 0U);
    EXPECT(g_appWatchdogDiagnostics.last_missing_heartbeat_mask ==
           APP_WATCHDOG_HEARTBEAT_RTC_SERVICE);

    App_Watchdog_SetTestInhibit(1U);
    CheckInAll();
    fake_tick += APP_WATCHDOG_REFRESH_PERIOD_MS;
    App_Watchdog_Process();
    EXPECT(fake_refresh_calls == 0U);

    return 0;
}

static int TestRefreshFailure(void)
{
    ResetFixture();
    EXPECT(App_Watchdog_Init() == HAL_OK);
    fake_refresh_status = HAL_ERROR;

    CheckInAll();
    fake_tick = APP_WATCHDOG_REFRESH_PERIOD_MS;
    App_Watchdog_Process();

    EXPECT(fake_refresh_calls == 1U);
    EXPECT(g_appWatchdogDiagnostics.refresh_count == 0U);
    EXPECT(g_appWatchdogDiagnostics.refresh_error_count == 1U);

    return 0;
}

static int TestTickWraparound(void)
{
    ResetFixture();
    fake_tick = UINT32_MAX - 99U;
    EXPECT(App_Watchdog_Init() == HAL_OK);
    CheckInAll();

    fake_tick = 200U;
    App_Watchdog_Process();
    EXPECT(fake_refresh_calls == 1U);

    return 0;
}

int main(void)
{
    EXPECT(TestInitialization() == 0);
    EXPECT(TestInitializationFailure() == 0);
    EXPECT(TestHealthyRefreshGate() == 0);
    EXPECT(TestMissingHeartbeatAndRecovery() == 0);
    EXPECT(TestFaultInjectionControls() == 0);
    EXPECT(TestRefreshFailure() == 0);
    EXPECT(TestTickWraparound() == 0);

    printf("PASS: watchdog initialization, health gate and tick wraparound\n");
    return 0;
}
