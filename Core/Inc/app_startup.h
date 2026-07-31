#ifndef APP_STARTUP_H
#define APP_STARTUP_H

#include <stdint.h>

typedef enum
{
    APP_STARTUP_RESOURCE_NONE = 0U,
    APP_STARTUP_RESOURCE_FDCAN = (1UL << 0),
    APP_STARTUP_RESOURCE_I2C = (1UL << 1),
    APP_STARTUP_RESOURCE_SPI = (1UL << 2),
    APP_STARTUP_RESOURCE_PWM_TIMER = (1UL << 3),
    APP_STARTUP_RESOURCE_CAPTURE_TIMER = (1UL << 4),
    APP_STARTUP_RESOURCE_PWM_CONTROL = (1UL << 5),
    APP_STARTUP_RESOURCE_INPUT_CAPTURE = (1UL << 6),
    APP_STARTUP_RESOURCE_COM = (1UL << 7)
} App_Startup_Resource_t;

#define APP_STARTUP_EXPECTED_RESOURCES \
    ((uint32_t)APP_STARTUP_RESOURCE_FDCAN | \
     (uint32_t)APP_STARTUP_RESOURCE_I2C | \
     (uint32_t)APP_STARTUP_RESOURCE_SPI | \
     (uint32_t)APP_STARTUP_RESOURCE_PWM_TIMER | \
     (uint32_t)APP_STARTUP_RESOURCE_CAPTURE_TIMER | \
     (uint32_t)APP_STARTUP_RESOURCE_PWM_CONTROL | \
     (uint32_t)APP_STARTUP_RESOURCE_INPUT_CAPTURE | \
     (uint32_t)APP_STARTUP_RESOURCE_COM)

typedef enum
{
    APP_STARTUP_RESULT_OK = 0U,
    APP_STARTUP_RESULT_FDCAN_HAL_INIT = 0x0101U,
    APP_STARTUP_RESULT_FDCAN_MSP_CLOCK = 0x0102U,
    APP_STARTUP_RESULT_I2C_HAL_INIT = 0x0201U,
    APP_STARTUP_RESULT_I2C_MSP_CLOCK = 0x0202U,
    APP_STARTUP_RESULT_I2C_ANALOG_FILTER = 0x0203U,
    APP_STARTUP_RESULT_I2C_DIGITAL_FILTER = 0x0204U,
    APP_STARTUP_RESULT_SPI_HAL_INIT = 0x0301U,
    APP_STARTUP_RESULT_SPI_MSP_CLOCK = 0x0302U,
    APP_STARTUP_RESULT_PWM_TIMER_BASE = 0x0401U,
    APP_STARTUP_RESULT_PWM_TIMER_CLOCK = 0x0402U,
    APP_STARTUP_RESULT_PWM_TIMER_PWM = 0x0403U,
    APP_STARTUP_RESULT_PWM_TIMER_MASTER = 0x0404U,
    APP_STARTUP_RESULT_PWM_TIMER_CHANNEL = 0x0405U,
    APP_STARTUP_RESULT_CAPTURE_TIMER_INIT = 0x0501U,
    APP_STARTUP_RESULT_CAPTURE_TIMER_SLAVE = 0x0502U,
    APP_STARTUP_RESULT_CAPTURE_TIMER_CHANNEL_1 = 0x0503U,
    APP_STARTUP_RESULT_CAPTURE_TIMER_CHANNEL_2 = 0x0504U,
    APP_STARTUP_RESULT_CAPTURE_TIMER_MASTER = 0x0505U,
    APP_STARTUP_RESULT_PWM_CONTROL_INIT = 0x0601U,
    APP_STARTUP_RESULT_PWM_CONTROL_DEFAULT = 0x0602U,
    APP_STARTUP_RESULT_INPUT_CAPTURE_INIT = 0x0701U,
    APP_STARTUP_RESULT_COM_INIT = 0x0801U
} App_Startup_Result_t;

typedef struct
{
    uint32_t expected_mask;
    uint32_t attempted_mask;
    uint32_t ready_mask;
    uint32_t failed_mask;
    uint32_t record_count;
    uint32_t failure_count;
    uint32_t first_failed_resource;
    uint32_t first_failure_result;
    uint32_t last_failed_resource;
    uint32_t last_failure_result;
    uint8_t degraded;
} App_Startup_Snapshot_t;

extern volatile App_Startup_Snapshot_t g_appStartup;

void App_Startup_Init(uint32_t expected_mask);
void App_Startup_Record(App_Startup_Resource_t resource,
                        App_Startup_Result_t result);
uint8_t App_Startup_IsReady(App_Startup_Resource_t resource);
void App_Startup_GetSnapshot(App_Startup_Snapshot_t *snapshot);

#endif /* APP_STARTUP_H */
