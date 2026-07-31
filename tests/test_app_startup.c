#include "unity.h"

#include "app_startup.h"

void setUp(void)
{
    App_Startup_Init(APP_STARTUP_EXPECTED_RESOURCES);
}

void tearDown(void)
{
}

static void test_successful_resources_become_ready(void)
{
    App_Startup_Snapshot_t snapshot;

    App_Startup_Record(APP_STARTUP_RESOURCE_FDCAN,
                       APP_STARTUP_RESULT_OK);
    App_Startup_Record(APP_STARTUP_RESOURCE_I2C,
                       APP_STARTUP_RESULT_OK);
    App_Startup_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(
        1U, App_Startup_IsReady(APP_STARTUP_RESOURCE_FDCAN));
    TEST_ASSERT_EQUAL_UINT8(
        1U, App_Startup_IsReady(APP_STARTUP_RESOURCE_I2C));
    TEST_ASSERT_EQUAL_UINT32(
        APP_STARTUP_RESOURCE_FDCAN | APP_STARTUP_RESOURCE_I2C,
        snapshot.ready_mask);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.failed_mask);
}

static void test_failure_latches_resource_and_diagnostic_code(void)
{
    App_Startup_Snapshot_t snapshot;

    App_Startup_Record(APP_STARTUP_RESOURCE_I2C,
                       APP_STARTUP_RESULT_I2C_ANALOG_FILTER);
    App_Startup_Record(APP_STARTUP_RESOURCE_SPI,
                       APP_STARTUP_RESULT_SPI_HAL_INIT);
    App_Startup_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(1U, snapshot.degraded);
    TEST_ASSERT_EQUAL_UINT32(
        APP_STARTUP_RESOURCE_I2C | APP_STARTUP_RESOURCE_SPI,
        snapshot.failed_mask);
    TEST_ASSERT_EQUAL_UINT32(APP_STARTUP_RESOURCE_I2C,
                             snapshot.first_failed_resource);
    TEST_ASSERT_EQUAL_UINT32(APP_STARTUP_RESULT_I2C_ANALOG_FILTER,
                             snapshot.first_failure_result);
    TEST_ASSERT_EQUAL_UINT32(APP_STARTUP_RESOURCE_SPI,
                             snapshot.last_failed_resource);
    TEST_ASSERT_EQUAL_UINT32(APP_STARTUP_RESULT_SPI_HAL_INIT,
                             snapshot.last_failure_result);
}

static void test_successful_retry_clears_failed_resource(void)
{
    App_Startup_Snapshot_t snapshot;

    App_Startup_Record(APP_STARTUP_RESOURCE_PWM_TIMER,
                       APP_STARTUP_RESULT_PWM_TIMER_PWM);
    App_Startup_Record(APP_STARTUP_RESOURCE_PWM_TIMER,
                       APP_STARTUP_RESULT_OK);
    App_Startup_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT8(
        1U, App_Startup_IsReady(APP_STARTUP_RESOURCE_PWM_TIMER));
    TEST_ASSERT_EQUAL_UINT32(0U,
        snapshot.failed_mask & APP_STARTUP_RESOURCE_PWM_TIMER);
    TEST_ASSERT_EQUAL_UINT32(1U, snapshot.failure_count);
}

static void test_invalid_or_unexpected_resource_is_ignored(void)
{
    App_Startup_Snapshot_t snapshot;

    App_Startup_Record(APP_STARTUP_RESOURCE_NONE,
                       APP_STARTUP_RESULT_FDCAN_HAL_INIT);
    App_Startup_Record(
        (App_Startup_Resource_t)(
            APP_STARTUP_RESOURCE_FDCAN | APP_STARTUP_RESOURCE_I2C),
        APP_STARTUP_RESULT_FDCAN_HAL_INIT);
    App_Startup_GetSnapshot(&snapshot);

    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.record_count);
    TEST_ASSERT_EQUAL_UINT32(0U, snapshot.attempted_mask);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_successful_resources_become_ready);
    RUN_TEST(test_failure_latches_resource_and_diagnostic_code);
    RUN_TEST(test_successful_retry_clears_failed_resource);
    RUN_TEST(test_invalid_or_unexpected_resource_is_ignored);
    return UNITY_END();
}
