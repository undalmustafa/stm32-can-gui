#include "pwm_self_test.h"

#include "app_log.h"
#include "input_capture.h"
#include "main.h"
#include "pwm_control.h"

#define PWM_SELF_TEST_SETTLE_MS          300U
#define PWM_SELF_TEST_TOTAL_POINTS       10U

typedef struct
{
    uint32_t frequency_hz;
    uint8_t duty_percent;
    uint8_t expect_signal;
} PWM_SelfTest_Point_t;

typedef enum
{
    PWM_SELF_TEST_PHASE_IDLE = 0,
    PWM_SELF_TEST_PHASE_APPLY,
    PWM_SELF_TEST_PHASE_SETTLE
} PWM_SelfTest_Phase_t;

static const PWM_SelfTest_Point_t test_points[PWM_SELF_TEST_TOTAL_POINTS] =
{
    {1000U, 50U, 1U},
    {10000U, 50U, 1U},
    {100000U, 50U, 1U},
    {500000U, 50U, 1U},
    {10000U, 10U, 1U},
    {10000U, 25U, 1U},
    {10000U, 50U, 1U},
    {10000U, 75U, 1U},
    {10000U, 90U, 1U},
    {0U, 0U, 0U}
};

static PWM_SelfTest_State_t self_test_state;
static PWM_SelfTest_Phase_t self_test_phase;
static PWM_Control_State_t saved_pwm_state;
static uint8_t point_index;
static uint8_t failed_points;
static uint16_t failed_point_mask;
static uint32_t phase_started_at;

static void PWM_SelfTest_LogTerminalResult(void)
{
    App_Log_Severity_t severity = APP_LOG_SEVERITY_INFO;
    uint32_t summary;

    if (self_test_state.state == PWM_SELF_TEST_STATE_FAILED)
    {
        severity = APP_LOG_SEVERITY_WARNING;
    }
    else if (self_test_state.state == PWM_SELF_TEST_STATE_ERROR)
    {
        severity = APP_LOG_SEVERITY_FAULT;
    }

    summary =
        ((uint32_t)self_test_state.state) |
        ((uint32_t)self_test_state.passed_points << 8) |
        ((uint32_t)self_test_state.total_points << 16) |
        ((uint32_t)failed_points << 24);

    (void)App_Log_Push(
        APP_LOG_SOURCE_PWM,
        severity,
        APP_LOG_EVENT_PWM_SELF_TEST_COMPLETED,
        summary,
        (uint32_t)failed_point_mask);
}

static uint32_t PWM_SelfTest_AbsoluteDifference(
    uint32_t first,
    uint32_t second)
{
    return (first >= second) ? (first - second) : (second - first);
}

static uint8_t PWM_SelfTest_FrequencyMatches(
    uint32_t measured,
    uint32_t expected)
{
    uint32_t tolerance = expected / 50U;

    if (tolerance < 2U)
    {
        tolerance = 2U;
    }

    return (PWM_SelfTest_AbsoluteDifference(measured, expected) <= tolerance)
        ? 1U
        : 0U;
}

static uint8_t PWM_SelfTest_DutyMatches(
    uint8_t measured,
    uint8_t expected)
{
    uint8_t difference = (measured >= expected)
        ? (uint8_t)(measured - expected)
        : (uint8_t)(expected - measured);

    return (difference <= 2U) ? 1U : 0U;
}

static PWM_SelfTest_ResultCode_t PWM_SelfTest_RestoreOutput(void)
{
    if (saved_pwm_state.requested_frequency_hz != 0U)
    {
        if (PWM_Control_Set(saved_pwm_state.requested_frequency_hz,
                            saved_pwm_state.duty_percent) != PWM_CONTROL_OK)
        {
            return PWM_SELF_TEST_CONTROL_ERROR;
        }
    }

    if (saved_pwm_state.running != 0U)
    {
        if (PWM_Control_Start() != PWM_CONTROL_OK)
        {
            return PWM_SELF_TEST_CONTROL_ERROR;
        }
    }
    else if (PWM_Control_Stop() != PWM_CONTROL_OK)
    {
        return PWM_SELF_TEST_CONTROL_ERROR;
    }

    return PWM_SELF_TEST_OK;
}

static PWM_SelfTest_ResultCode_t PWM_SelfTest_ApplyPoint(void)
{
    const PWM_SelfTest_Point_t *point = &test_points[point_index];

    if (point->expect_signal == 0U)
    {
        return (PWM_Control_Stop() == PWM_CONTROL_OK)
            ? PWM_SELF_TEST_OK
            : PWM_SELF_TEST_CONTROL_ERROR;
    }

    if (PWM_Control_Set(point->frequency_hz,
                        point->duty_percent) != PWM_CONTROL_OK)
    {
        return PWM_SELF_TEST_CONTROL_ERROR;
    }

    return (PWM_Control_Start() == PWM_CONTROL_OK)
        ? PWM_SELF_TEST_OK
        : PWM_SELF_TEST_CONTROL_ERROR;
}

static void PWM_SelfTest_RecordPoint(void)
{
    const PWM_SelfTest_Point_t *point = &test_points[point_index];
    Input_Capture_State_t capture = Input_Capture_GetState();
    PWM_Control_State_t output = PWM_Control_GetState();
    uint8_t passed;

    if (point->expect_signal == 0U)
    {
        passed = (capture.signal_detected == 0U) ? 1U : 0U;
    }
    else
    {
        passed = (
            (output.running != 0U) &&
            (capture.signal_detected != 0U) &&
            (PWM_SelfTest_FrequencyMatches(
                capture.frequency_hz,
                output.actual_frequency_hz) != 0U) &&
            (PWM_SelfTest_DutyMatches(
                capture.duty_percent,
                output.duty_percent) != 0U))
            ? 1U
            : 0U;
    }

    self_test_state.last_result.point = (uint8_t)(point_index + 1U);
    self_test_state.last_result.passed = passed;
    self_test_state.last_result.expected_duty_percent =
        point->duty_percent;
    self_test_state.last_result.measured_duty_percent =
        capture.duty_percent;
    self_test_state.last_result.expected_frequency_hz =
        point->frequency_hz;
    self_test_state.last_result.measured_frequency_hz =
        capture.frequency_hz;
    self_test_state.result_sequence++;

    if (passed != 0U)
    {
        self_test_state.passed_points++;
    }
    else
    {
        failed_points++;
        failed_point_mask |= (uint16_t)(1UL << point_index);
    }
}

void PWM_SelfTest_Init(void)
{
    self_test_state = (PWM_SelfTest_State_t){0};
    self_test_state.state = PWM_SELF_TEST_STATE_IDLE;
    self_test_state.total_points = PWM_SELF_TEST_TOTAL_POINTS;
    self_test_phase = PWM_SELF_TEST_PHASE_IDLE;
}

PWM_SelfTest_ResultCode_t PWM_SelfTest_Start(void)
{
    if (self_test_state.state == PWM_SELF_TEST_STATE_RUNNING)
    {
        return PWM_SELF_TEST_BUSY;
    }

    saved_pwm_state = PWM_Control_GetState();
    point_index = 0U;
    failed_points = 0U;
    failed_point_mask = 0U;
    self_test_state.state = PWM_SELF_TEST_STATE_RUNNING;
    self_test_state.current_point = 1U;
    self_test_state.total_points = PWM_SELF_TEST_TOTAL_POINTS;
    self_test_state.passed_points = 0U;
    self_test_state.expected_frequency_hz = test_points[0].frequency_hz;
    self_test_state.expected_duty_percent = test_points[0].duty_percent;
    self_test_phase = PWM_SELF_TEST_PHASE_APPLY;
    return PWM_SELF_TEST_OK;
}

void PWM_SelfTest_Cancel(void)
{
    if (self_test_state.state != PWM_SELF_TEST_STATE_RUNNING)
    {
        return;
    }

    self_test_phase = PWM_SELF_TEST_PHASE_IDLE;
    self_test_state.state =
        (PWM_SelfTest_RestoreOutput() == PWM_SELF_TEST_OK)
        ? PWM_SELF_TEST_STATE_CANCELLED
        : PWM_SELF_TEST_STATE_ERROR;
    PWM_SelfTest_LogTerminalResult();
}

void PWM_SelfTest_Process(void)
{
    uint32_t now;

    if (self_test_state.state != PWM_SELF_TEST_STATE_RUNNING)
    {
        return;
    }

    now = HAL_GetTick();
    if (self_test_phase == PWM_SELF_TEST_PHASE_APPLY)
    {
        if (PWM_SelfTest_ApplyPoint() != PWM_SELF_TEST_OK)
        {
            self_test_state.state = PWM_SELF_TEST_STATE_ERROR;
            self_test_phase = PWM_SELF_TEST_PHASE_IDLE;
            (void)PWM_SelfTest_RestoreOutput();
            PWM_SelfTest_LogTerminalResult();
            return;
        }

        phase_started_at = now;
        self_test_phase = PWM_SELF_TEST_PHASE_SETTLE;
        return;
    }

    if ((self_test_phase != PWM_SELF_TEST_PHASE_SETTLE) ||
        ((now - phase_started_at) < PWM_SELF_TEST_SETTLE_MS))
    {
        return;
    }

    PWM_SelfTest_RecordPoint();
    point_index++;

    if (point_index >= PWM_SELF_TEST_TOTAL_POINTS)
    {
        self_test_phase = PWM_SELF_TEST_PHASE_IDLE;
        if (PWM_SelfTest_RestoreOutput() != PWM_SELF_TEST_OK)
        {
            self_test_state.state = PWM_SELF_TEST_STATE_ERROR;
        }
        else
        {
            self_test_state.state = (failed_points == 0U)
                ? PWM_SELF_TEST_STATE_PASSED
                : PWM_SELF_TEST_STATE_FAILED;
        }
        PWM_SelfTest_LogTerminalResult();
        return;
    }

    self_test_state.current_point = (uint8_t)(point_index + 1U);
    self_test_state.expected_frequency_hz =
        test_points[point_index].frequency_hz;
    self_test_state.expected_duty_percent =
        test_points[point_index].duty_percent;
    self_test_phase = PWM_SELF_TEST_PHASE_APPLY;
}

uint8_t PWM_SelfTest_IsRunning(void)
{
    return (self_test_state.state == PWM_SELF_TEST_STATE_RUNNING) ? 1U : 0U;
}

PWM_SelfTest_State_t PWM_SelfTest_GetState(void)
{
    return self_test_state;
}
