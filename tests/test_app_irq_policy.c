#include "unity.h"

#include "app_irq_policy.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_default_priority_contract_is_valid(void)
{
    App_IrqPolicy_Config_t config;

    App_IrqPolicy_GetDefault(&config);

    TEST_ASSERT_EQUAL_UINT8(APP_IRQ_PRIORITY_SAFETY_TIMER,
                            config.safety_timer);
    TEST_ASSERT_EQUAL_UINT8(APP_IRQ_PRIORITY_SYSTICK, config.systick);
    TEST_ASSERT_EQUAL_UINT8(APP_IRQ_PRIORITY_FDCAN1, config.fdcan1);
    TEST_ASSERT_EQUAL_UINT8(APP_IRQ_PRIORITY_TIC12400_EXTI,
                            config.tic12400_exti);
    TEST_ASSERT_EQUAL_UINT8(APP_IRQ_PRIORITY_APPLICATION_TIMER,
                            config.application_timer);
    TEST_ASSERT_EQUAL_UINT8(APP_IRQ_PRIORITY_USER_BUTTON,
                            config.user_button);
    TEST_ASSERT_EQUAL_UINT32(APP_IRQ_POLICY_VALID,
                             App_IrqPolicy_Validate(&config));
}

void test_timeout_source_cannot_share_fdcan_priority(void)
{
    App_IrqPolicy_Config_t config;
    uint32_t result;

    App_IrqPolicy_GetDefault(&config);
    config.systick = config.fdcan1;

    result = App_IrqPolicy_Validate(&config);
    TEST_ASSERT_BITS(APP_IRQ_POLICY_TIMEOUT_SOURCE_BLOCKED,
                     APP_IRQ_POLICY_TIMEOUT_SOURCE_BLOCKED,
                     result);
}

void test_timeout_source_cannot_be_lower_than_exti_or_timer(void)
{
    App_IrqPolicy_Config_t config;
    uint32_t result;

    App_IrqPolicy_GetDefault(&config);
    config.systick = config.application_timer;

    result = App_IrqPolicy_Validate(&config);
    TEST_ASSERT_BITS(APP_IRQ_POLICY_TIMEOUT_SOURCE_BLOCKED,
                     APP_IRQ_POLICY_TIMEOUT_SOURCE_BLOCKED,
                     result);
}

void test_safety_timer_must_preempt_timeout_source(void)
{
    App_IrqPolicy_Config_t config;
    uint32_t result;

    App_IrqPolicy_GetDefault(&config);
    config.safety_timer = config.systick;

    result = App_IrqPolicy_Validate(&config);
    TEST_ASSERT_BITS(APP_IRQ_POLICY_SAFETY_TIMER_BLOCKED,
                     APP_IRQ_POLICY_SAFETY_TIMER_BLOCKED,
                     result);
}

void test_invalid_service_order_and_range_are_reported(void)
{
    App_IrqPolicy_Config_t config;
    uint32_t result;

    App_IrqPolicy_GetDefault(&config);
    config.fdcan1 = config.tic12400_exti;
    config.application_timer = APP_IRQ_PRIORITY_LEVEL_COUNT;
    config.user_button = APP_IRQ_PRIORITY_LEVEL_COUNT;

    result = App_IrqPolicy_Validate(&config);
    TEST_ASSERT_BITS(APP_IRQ_POLICY_PRIORITY_OUT_OF_RANGE,
                     APP_IRQ_POLICY_PRIORITY_OUT_OF_RANGE,
                     result);
    TEST_ASSERT_BITS(APP_IRQ_POLICY_SERVICE_ORDER_INVALID,
                     APP_IRQ_POLICY_SERVICE_ORDER_INVALID,
                     result);
    TEST_ASSERT_BITS(APP_IRQ_POLICY_BACKGROUND_ORDER_INVALID,
                     APP_IRQ_POLICY_BACKGROUND_ORDER_INVALID,
                     result);
}

void test_null_configuration_is_rejected(void)
{
    TEST_ASSERT_EQUAL_UINT32(APP_IRQ_POLICY_NULL_CONFIG,
                             App_IrqPolicy_Validate(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_priority_contract_is_valid);
    RUN_TEST(test_timeout_source_cannot_share_fdcan_priority);
    RUN_TEST(test_timeout_source_cannot_be_lower_than_exti_or_timer);
    RUN_TEST(test_safety_timer_must_preempt_timeout_source);
    RUN_TEST(test_invalid_service_order_and_range_are_reported);
    RUN_TEST(test_null_configuration_is_rejected);
    return UNITY_END();
}
