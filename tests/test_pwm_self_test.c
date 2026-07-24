#include "unity.h"

#include "app_log.h"
#include "input_capture.h"
#include "pwm_control.h"
#include "pwm_self_test.h"

static uint32_t fake_tick;
static PWM_Control_State_t fake_pwm;
static Input_Capture_State_t fake_capture;
static uint8_t force_frequency_failure;
static uint32_t logged_data_0;
static uint32_t logged_data_1;
static uint16_t logged_event_code;
static uint8_t logged_source;
static uint8_t logged_severity;
static uint8_t log_count;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

uint8_t App_Log_Push(App_Log_Source_t source,
                     App_Log_Severity_t severity,
                     App_Log_EventCode_t event_code,
                     uint32_t data_0,
                     uint32_t data_1)
{
    logged_source = (uint8_t)source;
    logged_severity = (uint8_t)severity;
    logged_event_code = (uint16_t)event_code;
    logged_data_0 = data_0;
    logged_data_1 = data_1;
    log_count++;
    return 1U;
}

PWM_Control_Result_t PWM_Control_Set(uint32_t frequency_hz,
                                     uint8_t duty_percent)
{
    fake_pwm.requested_frequency_hz = frequency_hz;
    fake_pwm.actual_frequency_hz = frequency_hz;
    fake_pwm.duty_percent = duty_percent;
    fake_capture.signal_detected = 1U;
    fake_capture.frequency_hz = frequency_hz;
    fake_capture.duty_percent = duty_percent;

    if ((force_frequency_failure != 0U) &&
        (frequency_hz == 100000U))
    {
        fake_capture.frequency_hz = 90000U;
    }

    return PWM_CONTROL_OK;
}

PWM_Control_Result_t PWM_Control_Stop(void)
{
    fake_pwm.running = 0U;
    fake_capture.signal_detected = 0U;
    fake_capture.frequency_hz = 0U;
    fake_capture.duty_percent = 0U;
    return PWM_CONTROL_OK;
}

PWM_Control_Result_t PWM_Control_Start(void)
{
    fake_pwm.running = 1U;
    return PWM_CONTROL_OK;
}

PWM_Control_State_t PWM_Control_GetState(void)
{
    return fake_pwm;
}

Input_Capture_State_t Input_Capture_GetState(void)
{
    return fake_capture;
}

void setUp(void)
{
    fake_tick = 0U;
    force_frequency_failure = 0U;
    logged_data_0 = 0U;
    logged_data_1 = 0U;
    logged_event_code = 0U;
    logged_source = 0U;
    logged_severity = 0U;
    log_count = 0U;
    fake_pwm.running = 1U;
    fake_pwm.requested_frequency_hz = 20000U;
    fake_pwm.actual_frequency_hz = 20000U;
    fake_pwm.duty_percent = 30U;
    fake_capture.signal_detected = 1U;
    fake_capture.frequency_hz = 20000U;
    fake_capture.duty_percent = 30U;
    PWM_SelfTest_Init();
}

void tearDown(void)
{
}

static void run_all_points(void)
{
    uint8_t point;

    for (point = 0U; point < 10U; point++)
    {
        PWM_SelfTest_Process();
        fake_tick += 300U;
        PWM_SelfTest_Process();
    }
}

void test_self_test_passes_and_restores_output(void)
{
    PWM_SelfTest_State_t state;

    TEST_ASSERT_EQUAL(PWM_SELF_TEST_OK, PWM_SelfTest_Start());
    run_all_points();
    state = PWM_SelfTest_GetState();

    TEST_ASSERT_EQUAL(PWM_SELF_TEST_STATE_PASSED, state.state);
    TEST_ASSERT_EQUAL_UINT8(10U, state.passed_points);
    TEST_ASSERT_EQUAL_UINT32(10U, state.result_sequence);
    TEST_ASSERT_EQUAL_UINT8(1U, fake_pwm.running);
    TEST_ASSERT_EQUAL_UINT32(20000U, fake_pwm.actual_frequency_hz);
    TEST_ASSERT_EQUAL_UINT8(30U, fake_pwm.duty_percent);
    TEST_ASSERT_EQUAL_UINT8(1U, log_count);
    TEST_ASSERT_EQUAL_UINT16(
        APP_LOG_EVENT_PWM_SELF_TEST_COMPLETED,
        logged_event_code);
    TEST_ASSERT_EQUAL_UINT8(APP_LOG_SOURCE_PWM, logged_source);
    TEST_ASSERT_EQUAL_UINT8(APP_LOG_SEVERITY_INFO, logged_severity);
    TEST_ASSERT_EQUAL_UINT32(0U, logged_data_1);
}

void test_self_test_reports_measurement_failure(void)
{
    PWM_SelfTest_State_t state;

    force_frequency_failure = 1U;
    TEST_ASSERT_EQUAL(PWM_SELF_TEST_OK, PWM_SelfTest_Start());
    run_all_points();
    state = PWM_SelfTest_GetState();

    TEST_ASSERT_EQUAL(PWM_SELF_TEST_STATE_FAILED, state.state);
    TEST_ASSERT_EQUAL_UINT8(9U, state.passed_points);
    TEST_ASSERT_EQUAL_UINT8(APP_LOG_SEVERITY_WARNING, logged_severity);
    TEST_ASSERT_EQUAL_UINT32(1UL << 2, logged_data_1);
    TEST_ASSERT_EQUAL_UINT8(1U, log_count);
}

void test_cancel_restores_output(void)
{
    PWM_SelfTest_State_t state;

    TEST_ASSERT_EQUAL(PWM_SELF_TEST_OK, PWM_SelfTest_Start());
    PWM_SelfTest_Process();
    PWM_SelfTest_Cancel();
    state = PWM_SelfTest_GetState();

    TEST_ASSERT_EQUAL(PWM_SELF_TEST_STATE_CANCELLED, state.state);
    TEST_ASSERT_EQUAL_UINT8(1U, fake_pwm.running);
    TEST_ASSERT_EQUAL_UINT32(20000U, fake_pwm.actual_frequency_hz);
    TEST_ASSERT_EQUAL_UINT8(30U, fake_pwm.duty_percent);
    TEST_ASSERT_EQUAL_UINT8(APP_LOG_SEVERITY_INFO, logged_severity);
    TEST_ASSERT_EQUAL_UINT8(1U, log_count);
    TEST_ASSERT_EQUAL_UINT8(
        PWM_SELF_TEST_STATE_CANCELLED,
        (uint8_t)(logged_data_0 & 0xFFU));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_self_test_passes_and_restores_output);
    RUN_TEST(test_self_test_reports_measurement_failure);
    RUN_TEST(test_cancel_restores_output);
    return UNITY_END();
}
