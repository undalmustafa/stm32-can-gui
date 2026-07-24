#include "unity.h"

#include "input_capture.h"
#include "pwm_control.h"
#include "pwm_self_test.h"

static uint32_t fake_tick;
static PWM_Control_State_t fake_pwm;
static Input_Capture_State_t fake_capture;
static uint8_t force_frequency_failure;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
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
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_self_test_passes_and_restores_output);
    RUN_TEST(test_self_test_reports_measurement_failure);
    RUN_TEST(test_cancel_restores_output);
    return UNITY_END();
}
