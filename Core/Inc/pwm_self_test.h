#ifndef PWM_SELF_TEST_H
#define PWM_SELF_TEST_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "can_protocol_generated.h"

typedef enum
{
    PWM_SELF_TEST_STATE_IDLE = CAN_PROTOCOL_PWM_SELF_TEST_STATE_IDLE,
    PWM_SELF_TEST_STATE_RUNNING = CAN_PROTOCOL_PWM_SELF_TEST_STATE_RUNNING,
    PWM_SELF_TEST_STATE_PASSED = CAN_PROTOCOL_PWM_SELF_TEST_STATE_PASSED,
    PWM_SELF_TEST_STATE_FAILED = CAN_PROTOCOL_PWM_SELF_TEST_STATE_FAILED,
    PWM_SELF_TEST_STATE_CANCELLED =
        CAN_PROTOCOL_PWM_SELF_TEST_STATE_CANCELLED,
    PWM_SELF_TEST_STATE_ERROR = CAN_PROTOCOL_PWM_SELF_TEST_STATE_ERROR
} PWM_SelfTest_StateCode_t;

typedef enum
{
    PWM_SELF_TEST_OK = 0,
    PWM_SELF_TEST_BUSY,
    PWM_SELF_TEST_CONTROL_ERROR
} PWM_SelfTest_ResultCode_t;

typedef struct
{
    uint8_t point;
    uint8_t passed;
    uint8_t expected_duty_percent;
    uint8_t measured_duty_percent;
    uint32_t expected_frequency_hz;
    uint32_t measured_frequency_hz;
} PWM_SelfTest_PointResult_t;

typedef struct
{
    PWM_SelfTest_StateCode_t state;
    uint8_t current_point;
    uint8_t total_points;
    uint8_t passed_points;
    uint32_t expected_frequency_hz;
    uint8_t expected_duty_percent;
    uint32_t result_sequence;
    PWM_SelfTest_PointResult_t last_result;
} PWM_SelfTest_State_t;

void PWM_SelfTest_Init(void);
PWM_SelfTest_ResultCode_t PWM_SelfTest_Start(void);
void PWM_SelfTest_Cancel(void);
void PWM_SelfTest_Process(void);
uint8_t PWM_SelfTest_IsRunning(void);
PWM_SelfTest_State_t PWM_SelfTest_GetState(void);

#endif /* PWM_SELF_TEST_H */
