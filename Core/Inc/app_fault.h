#ifndef APP_FAULT_H
#define APP_FAULT_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: App_Fault_RecordExceptionFromIsr
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#define APP_FAULT_RECORD_MAGIC          0x464C5431UL
#define APP_FAULT_RECORD_VERSION        1U
#define APP_FAULT_RECORD_COMMIT_MARKER  0xC04D17EDUL

typedef enum
{
    APP_FAULT_NONE = 0U,
    APP_FAULT_NMI = 1U,
    APP_FAULT_HARD = 2U,
    APP_FAULT_MEM_MANAGE = 3U,
    APP_FAULT_BUS = 4U,
    APP_FAULT_USAGE = 5U,
    APP_FAULT_ERROR_HANDLER = 6U
} App_Fault_Type_t;

typedef struct
{
    uint32_t sequence;
    uint32_t type;
    uint32_t exception_return;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t shcsr;
    uint32_t icsr;
    uint32_t capture_count;
    uint32_t validation_error_count;
    uint8_t valid;
    uint8_t storage_ready;
} App_Fault_Snapshot_t;

/*
 * Call once immediately after HAL_Init(). It enables backup SRAM, captures one
 * committed fault from the previous boot, and consumes the retained record.
 */
void App_Fault_CaptureBoot(void);
void App_Fault_GetSnapshot(App_Fault_Snapshot_t *snapshot);

/*
 * Fault-context API. The stack pointer may be NULL when no exception frame is
 * available. This function does not allocate, block, or call the HAL.
 */
void App_Fault_RecordExceptionFromIsr(App_Fault_Type_t type,
                                      const uint32_t *stack_frame,
                                      uint32_t exception_return);

#if defined(APP_FAULT_HOST_TEST)
void App_Fault_TestResetStorage(void);
void App_Fault_TestCorruptChecksum(void);
#endif

#endif /* APP_FAULT_H */
