#ifndef APP_TIMING_H
#define APP_TIMING_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#define APP_TIMING_ACK_BIN_COUNT 9U

typedef enum
{
    APP_TIMING_SERVICE_MAIN_LOOP = 0U,
    APP_TIMING_SERVICE_TIC12400_PROBE,
    APP_TIMING_SERVICE_CONTROL,
    APP_TIMING_SERVICE_RTC,
    APP_TIMING_SERVICE_INPUT_CAPTURE,
    APP_TIMING_SERVICE_CAN_APP,
    APP_TIMING_SERVICE_TIC12400_CAN,
    APP_TIMING_SERVICE_WATCHDOG,
    APP_TIMING_SERVICE_COUNT
} App_Timing_Service_t;

typedef struct
{
    uint32_t sample_count;
    uint32_t current_cycles;
    uint32_t minimum_cycles;
    uint32_t maximum_cycles;
    uint32_t budget_cycles;
    uint32_t overrun_count;
} App_Timing_ServiceStats_t;

typedef struct
{
    uint32_t sample_count;
    uint32_t current_cycles;
    uint32_t minimum_cycles;
    uint32_t maximum_cycles;
    uint32_t bins[APP_TIMING_ACK_BIN_COUNT];
} App_Timing_AckStats_t;

typedef struct
{
    uint32_t core_clock_hz;
    uint32_t init_count;
    App_Timing_ServiceStats_t service[APP_TIMING_SERVICE_COUNT];
    App_Timing_AckStats_t ack;
    uint8_t enabled;
} App_Timing_Snapshot_t;

typedef struct
{
    uint32_t sample_count;
    uint32_t current_us;
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t maximum_us;
} App_Timing_AckSummary_t;

/* Exposed for debugger inspection. Application code must use the API. */
extern volatile App_Timing_Snapshot_t g_appTiming;

void App_Timing_Init(uint32_t core_clock_hz);
uint32_t App_Timing_Now(void);
uint32_t App_Timing_Begin(void);
void App_Timing_End(App_Timing_Service_t service, uint32_t start_cycles);

void App_Timing_RecordElapsed(App_Timing_Service_t service,
                              uint32_t start_cycles,
                              uint32_t end_cycles);
void App_Timing_RecordAckElapsed(uint32_t start_cycles,
                                 uint32_t end_cycles);

uint32_t App_Timing_CyclesToMicroseconds(uint32_t cycles);
void App_Timing_GetSnapshot(App_Timing_Snapshot_t *snapshot);
void App_Timing_GetAckSummary(App_Timing_AckSummary_t *summary);

#endif /* APP_TIMING_H */
