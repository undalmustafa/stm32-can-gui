#include "app_fault.h"

#include <stddef.h>

#if !defined(APP_FAULT_HOST_TEST)
#include "main.h"
#endif

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
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
    uint32_t checksum;
    uint32_t commit_marker;
} App_Fault_RetainedRecord_t;

#if defined(__GNUC__) && !defined(APP_FAULT_HOST_TEST)
#define APP_FAULT_RETAINED_SECTION \
    __attribute__((section(".fault_retained"), used, aligned(32)))
#else
#define APP_FAULT_RETAINED_SECTION
#endif

static volatile App_Fault_RetainedRecord_t
    app_fault_retained_record APP_FAULT_RETAINED_SECTION;
static uint32_t app_fault_sequence;
static uint8_t app_fault_storage_ready;
static App_Fault_Snapshot_t app_fault_snapshot;

_Static_assert(sizeof(App_Fault_RetainedRecord_t) == 92U,
               "Fault retained record layout changed");
_Static_assert(offsetof(App_Fault_RetainedRecord_t, checksum) == 84U,
               "Fault retained checksum offset changed");

static uint32_t App_Fault_CalculateChecksum(const uint8_t *data,
                                            uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t byte_index;
    uint8_t bit_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= data[byte_index];

        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static uint8_t App_Fault_IsTypeValid(uint32_t type)
{
    return ((type >= (uint32_t)APP_FAULT_NMI) &&
            (type <= (uint32_t)APP_FAULT_ERROR_HANDLER)) ? 1U : 0U;
}

static uint8_t App_Fault_IsRecordValid(
    const App_Fault_RetainedRecord_t *record)
{
    uint32_t checksum;

    if ((record->magic != APP_FAULT_RECORD_MAGIC) ||
        (record->version != APP_FAULT_RECORD_VERSION) ||
        (record->size != sizeof(App_Fault_RetainedRecord_t)) ||
        (record->sequence == 0U) ||
        (App_Fault_IsTypeValid(record->type) == 0U) ||
        (record->commit_marker != APP_FAULT_RECORD_COMMIT_MARKER))
    {
        return 0U;
    }

    checksum = App_Fault_CalculateChecksum(
        (const uint8_t *)record,
        (uint32_t)offsetof(App_Fault_RetainedRecord_t, checksum));
    return (checksum == record->checksum) ? 1U : 0U;
}

static uint8_t App_Fault_EnableStorage(void)
{
#if defined(APP_FAULT_HOST_TEST)
    return 1U;
#else
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    return (HAL_PWREx_EnableBkUpReg() == HAL_OK) ? 1U : 0U;
#endif
}

static void App_Fault_CleanStorage(void)
{
#if !defined(APP_FAULT_HOST_TEST)
    SCB_CleanDCache_by_Addr(
        (uint32_t *)&app_fault_retained_record,
        (int32_t)sizeof(app_fault_retained_record));
    __DSB();
#endif
}

static void App_Fault_CopyToSnapshot(
    const App_Fault_RetainedRecord_t *record)
{
    app_fault_snapshot.sequence = record->sequence;
    app_fault_snapshot.type = record->type;
    app_fault_snapshot.exception_return = record->exception_return;
    app_fault_snapshot.r0 = record->r0;
    app_fault_snapshot.r1 = record->r1;
    app_fault_snapshot.r2 = record->r2;
    app_fault_snapshot.r3 = record->r3;
    app_fault_snapshot.r12 = record->r12;
    app_fault_snapshot.lr = record->lr;
    app_fault_snapshot.pc = record->pc;
    app_fault_snapshot.xpsr = record->xpsr;
    app_fault_snapshot.cfsr = record->cfsr;
    app_fault_snapshot.hfsr = record->hfsr;
    app_fault_snapshot.dfsr = record->dfsr;
    app_fault_snapshot.afsr = record->afsr;
    app_fault_snapshot.mmfar = record->mmfar;
    app_fault_snapshot.bfar = record->bfar;
    app_fault_snapshot.shcsr = record->shcsr;
    app_fault_snapshot.icsr = record->icsr;
    app_fault_snapshot.valid = 1U;
}

void App_Fault_CaptureBoot(void)
{
    App_Fault_RetainedRecord_t record;

    app_fault_snapshot = (App_Fault_Snapshot_t){0};
    app_fault_snapshot.capture_count = 1U;
    app_fault_storage_ready = App_Fault_EnableStorage();
    app_fault_snapshot.storage_ready = app_fault_storage_ready;

    if (app_fault_storage_ready == 0U)
    {
        return;
    }

    record = app_fault_retained_record;
    if (App_Fault_IsRecordValid(&record) != 0U)
    {
        app_fault_sequence = record.sequence;
        App_Fault_CopyToSnapshot(&record);
    }
    else if (record.commit_marker == APP_FAULT_RECORD_COMMIT_MARKER)
    {
        app_fault_snapshot.validation_error_count = 1U;
    }

    app_fault_retained_record.commit_marker = 0U;
    App_Fault_CleanStorage();
}

void App_Fault_GetSnapshot(App_Fault_Snapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        *snapshot = app_fault_snapshot;
    }
}

void App_Fault_RecordExceptionFromIsr(App_Fault_Type_t type,
                                      const uint32_t *stack_frame,
                                      uint32_t exception_return)
{
    App_Fault_RetainedRecord_t record = {0};

    if ((app_fault_storage_ready == 0U) ||
        (App_Fault_IsTypeValid((uint32_t)type) == 0U))
    {
        return;
    }

    app_fault_sequence++;
    if (app_fault_sequence == 0U)
    {
        app_fault_sequence = 1U;
    }

    record.magic = APP_FAULT_RECORD_MAGIC;
    record.version = APP_FAULT_RECORD_VERSION;
    record.size = sizeof(App_Fault_RetainedRecord_t);
    record.sequence = app_fault_sequence;
    record.type = (uint32_t)type;
    record.exception_return = exception_return;

    if (stack_frame != NULL)
    {
        record.r0 = stack_frame[0];
        record.r1 = stack_frame[1];
        record.r2 = stack_frame[2];
        record.r3 = stack_frame[3];
        record.r12 = stack_frame[4];
        record.lr = stack_frame[5];
        record.pc = stack_frame[6];
        record.xpsr = stack_frame[7];
    }
    else
    {
        record.pc = exception_return;
    }

#if !defined(APP_FAULT_HOST_TEST)
    record.cfsr = SCB->CFSR;
    record.hfsr = SCB->HFSR;
    record.dfsr = SCB->DFSR;
    record.afsr = SCB->AFSR;
    record.mmfar = SCB->MMFAR;
    record.bfar = SCB->BFAR;
    record.shcsr = SCB->SHCSR;
    record.icsr = SCB->ICSR;
#endif

    record.checksum = App_Fault_CalculateChecksum(
        (const uint8_t *)&record,
        (uint32_t)offsetof(App_Fault_RetainedRecord_t, checksum));

    app_fault_retained_record.commit_marker = 0U;
    app_fault_retained_record = record;
#if !defined(APP_FAULT_HOST_TEST)
    __DMB();
#endif
    app_fault_retained_record.commit_marker =
        APP_FAULT_RECORD_COMMIT_MARKER;
#if !defined(APP_FAULT_HOST_TEST)
    __DMB();
#endif
    App_Fault_CleanStorage();
}

#if defined(APP_FAULT_HOST_TEST)
void App_Fault_TestResetStorage(void)
{
    app_fault_retained_record = (App_Fault_RetainedRecord_t){0};
    app_fault_sequence = 0U;
    app_fault_storage_ready = 0U;
    app_fault_snapshot = (App_Fault_Snapshot_t){0};
}

void App_Fault_TestCorruptChecksum(void)
{
    app_fault_retained_record.checksum ^= 1UL;
}
#endif
