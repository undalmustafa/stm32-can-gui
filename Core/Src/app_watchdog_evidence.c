#include "app_watchdog_evidence.h"

#include <stddef.h>

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint32_t cause;
    uint32_t required_heartbeat_mask;
    uint32_t observed_heartbeat_mask;
    uint32_t missing_heartbeat_mask;
    uint32_t health_check_tick;
    uint32_t refresh_tick;
    uint32_t checksum;
    uint32_t commit_marker;
} App_Watchdog_RetainedRecord_t;

#if defined(__GNUC__)
#define APP_WATCHDOG_RETAINED_SECTION \
    __attribute__((section(".watchdog_retained"), used, aligned(32)))
#else
#define APP_WATCHDOG_RETAINED_SECTION
#endif

static volatile App_Watchdog_RetainedRecord_t
    app_watchdog_retained_record APP_WATCHDOG_RETAINED_SECTION;
static uint32_t app_watchdog_evidence_sequence;
static uint8_t app_watchdog_evidence_storage_ready;

volatile App_Watchdog_ResetEvidence_t g_appWatchdogResetEvidence;

_Static_assert(sizeof(App_Watchdog_RetainedRecord_t) == 44U,
               "Watchdog retained record layout changed");
_Static_assert(offsetof(App_Watchdog_RetainedRecord_t, checksum) == 36U,
               "Watchdog retained checksum offset changed");

static uint32_t App_Watchdog_Evidence_CalculateChecksum(
    const uint8_t *data,
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

static uint8_t App_Watchdog_Evidence_IsCauseValid(uint32_t cause)
{
    return ((cause >= (uint32_t)APP_WATCHDOG_EVIDENCE_HARD_STALL) &&
            (cause <= (uint32_t)APP_WATCHDOG_EVIDENCE_REFRESH_ERROR)) ?
        1U : 0U;
}

static uint8_t App_Watchdog_Evidence_IsRecordValid(
    const App_Watchdog_RetainedRecord_t *record)
{
    uint32_t checksum;

    if ((record->magic != APP_WATCHDOG_EVIDENCE_MAGIC) ||
        (record->version != APP_WATCHDOG_EVIDENCE_VERSION) ||
        (record->size != sizeof(App_Watchdog_RetainedRecord_t)) ||
        (record->commit_marker != APP_WATCHDOG_EVIDENCE_COMMIT_MARKER) ||
        (record->sequence == 0U) ||
        (App_Watchdog_Evidence_IsCauseValid(record->cause) == 0U))
    {
        return 0U;
    }

    checksum = App_Watchdog_Evidence_CalculateChecksum(
        (const uint8_t *)record,
        (uint32_t)offsetof(App_Watchdog_RetainedRecord_t, checksum));
    return (checksum == record->checksum) ? 1U : 0U;
}

static HAL_StatusTypeDef App_Watchdog_Evidence_EnableStorage(void)
{
#if defined(APP_WATCHDOG_HOST_TEST)
    return HAL_OK;
#else
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    return HAL_PWREx_EnableBkUpReg();
#endif
}

static void App_Watchdog_Evidence_CleanStorage(void)
{
#if !defined(APP_WATCHDOG_HOST_TEST)
    SCB_CleanDCache_by_Addr(
        (uint32_t *)&app_watchdog_retained_record,
        (int32_t)sizeof(app_watchdog_retained_record));
    __DSB();
#endif
}

static void App_Watchdog_Evidence_Commit(
    const App_Watchdog_RetainedRecord_t *record)
{
    app_watchdog_retained_record.commit_marker = 0U;
    app_watchdog_retained_record = *record;

#if !defined(APP_WATCHDOG_HOST_TEST)
    __DMB();
#endif

    app_watchdog_retained_record.commit_marker =
        APP_WATCHDOG_EVIDENCE_COMMIT_MARKER;

#if !defined(APP_WATCHDOG_HOST_TEST)
    __DMB();
#endif
    App_Watchdog_Evidence_CleanStorage();
}

void App_Watchdog_Evidence_CaptureBoot(uint8_t was_iwdg_reset)
{
    App_Watchdog_RetainedRecord_t record;
    uint8_t record_valid;

    g_appWatchdogResetEvidence = (App_Watchdog_ResetEvidence_t){0};
    g_appWatchdogResetEvidence.capture_count = 1U;
    app_watchdog_evidence_storage_ready =
        (App_Watchdog_Evidence_EnableStorage() == HAL_OK) ? 1U : 0U;
    g_appWatchdogResetEvidence.storage_ready =
        app_watchdog_evidence_storage_ready;

    if (app_watchdog_evidence_storage_ready == 0U)
    {
        return;
    }

    record = app_watchdog_retained_record;
    record_valid = App_Watchdog_Evidence_IsRecordValid(&record);

    if (record_valid != 0U)
    {
        app_watchdog_evidence_sequence = record.sequence;
    }

    if (was_iwdg_reset == 0U)
    {
        if (record_valid != 0U)
        {
            g_appWatchdogResetEvidence.stale_record_count = 1U;
        }
    }
    else if (record_valid != 0U)
    {
        g_appWatchdogResetEvidence.sequence = record.sequence;
        g_appWatchdogResetEvidence.cause = record.cause;
        g_appWatchdogResetEvidence.required_heartbeat_mask =
            record.required_heartbeat_mask;
        g_appWatchdogResetEvidence.observed_heartbeat_mask =
            record.observed_heartbeat_mask;
        g_appWatchdogResetEvidence.missing_heartbeat_mask =
            record.missing_heartbeat_mask;
        g_appWatchdogResetEvidence.health_check_tick =
            record.health_check_tick;
        g_appWatchdogResetEvidence.refresh_tick = record.refresh_tick;
        g_appWatchdogResetEvidence.valid = 1U;
    }
    else if (record.commit_marker ==
             APP_WATCHDOG_EVIDENCE_COMMIT_MARKER)
    {
        g_appWatchdogResetEvidence.validation_error_count = 1U;
    }

    app_watchdog_retained_record.commit_marker = 0U;
    App_Watchdog_Evidence_CleanStorage();
}

void App_Watchdog_Evidence_Record(
    App_Watchdog_EvidenceCause_t cause,
    uint32_t required_heartbeat_mask,
    uint32_t observed_heartbeat_mask,
    uint32_t missing_heartbeat_mask,
    uint32_t health_check_tick,
    uint32_t refresh_tick)
{
    App_Watchdog_RetainedRecord_t record = {0};

    if ((app_watchdog_evidence_storage_ready == 0U) ||
        (App_Watchdog_Evidence_IsCauseValid((uint32_t)cause) == 0U))
    {
        return;
    }

    app_watchdog_evidence_sequence++;
    if (app_watchdog_evidence_sequence == 0U)
    {
        app_watchdog_evidence_sequence = 1U;
    }

    record.magic = APP_WATCHDOG_EVIDENCE_MAGIC;
    record.version = APP_WATCHDOG_EVIDENCE_VERSION;
    record.size = sizeof(App_Watchdog_RetainedRecord_t);
    record.sequence = app_watchdog_evidence_sequence;
    record.cause = (uint32_t)cause;
    record.required_heartbeat_mask = required_heartbeat_mask;
    record.observed_heartbeat_mask = observed_heartbeat_mask;
    record.missing_heartbeat_mask = missing_heartbeat_mask;
    record.health_check_tick = health_check_tick;
    record.refresh_tick = refresh_tick;
    record.checksum = App_Watchdog_Evidence_CalculateChecksum(
        (const uint8_t *)&record,
        (uint32_t)offsetof(App_Watchdog_RetainedRecord_t, checksum));

    App_Watchdog_Evidence_Commit(&record);
}

void App_Watchdog_Evidence_Get(App_Watchdog_ResetEvidence_t *evidence)
{
    if (evidence != NULL)
    {
        *evidence = g_appWatchdogResetEvidence;
    }
}

#if APP_WATCHDOG_TEST_SUPPORT_ENABLED
void App_Watchdog_Evidence_TestResetStorage(void)
{
    app_watchdog_retained_record = (App_Watchdog_RetainedRecord_t){0};
    g_appWatchdogResetEvidence = (App_Watchdog_ResetEvidence_t){0};
    app_watchdog_evidence_sequence = 0U;
    app_watchdog_evidence_storage_ready = 0U;
}

void App_Watchdog_Evidence_TestCorruptChecksum(void)
{
    app_watchdog_retained_record.checksum ^= 1UL;
}
#endif
