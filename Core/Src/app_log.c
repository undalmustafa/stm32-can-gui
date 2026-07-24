#include "main.h"
#include "app_log.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__) && !defined(APP_LOG_HOST_TEST)
#define APP_LOG_RETAINED_SECTION \
    __attribute__((section(".app_log_retained"), used, aligned(32)))
#else
#define APP_LOG_RETAINED_SECTION
#endif

App_Log_StorageSlot_t
    g_appLogRecords[APP_LOG_RAM_CAPACITY] APP_LOG_RETAINED_SECTION;
volatile App_Log_Debug_t g_appLogDebug;

static uint16_t app_log_head_index;
static uint16_t app_log_tail_index;
static uint16_t app_log_count;
static uint32_t app_log_next_sequence;

_Static_assert(sizeof(App_Log_Record_t) == 32U,
               "App_Log_Record_t must remain 32 bytes");
_Static_assert(offsetof(App_Log_Record_t, crc16) == 28U,
               "CRC field offset must remain stable");
_Static_assert(sizeof(App_Log_StorageSlot_t) == 32U,
               "retained log slot must remain one cache line");

static uint32_t App_Log_EnterCritical(void)
{
#if defined(APP_LOG_HOST_TEST)
    return 0U;
#else
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
#endif
}

static void App_Log_ExitCritical(uint32_t primask)
{
#if defined(APP_LOG_HOST_TEST)
    (void)primask;
#else
    if (primask == 0U)
    {
        __enable_irq();
    }
#endif
}

static uint16_t App_Log_CalculateCrc16(const uint8_t *data,
                                      uint32_t length)
{
    uint16_t crc = 0xFFFFU;
    uint32_t byte_index;
    uint8_t bit_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= (uint16_t)data[byte_index] << 8;

        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint8_t App_Log_IsInputValid(App_Log_Source_t source,
                                    App_Log_Severity_t severity)
{
    if ((uint32_t)source > (uint32_t)APP_LOG_SOURCE_PWM)
    {
        return 0U;
    }

    if ((uint32_t)severity > (uint32_t)APP_LOG_SEVERITY_FAULT)
    {
        return 0U;
    }

    return 1U;
}

static void App_Log_UpdateDebugAfterWrite(
    const App_Log_Record_t *record)
{
    g_appLogDebug.write_count++;
    g_appLogDebug.next_sequence = app_log_next_sequence;
    g_appLogDebug.last_sequence = record->sequence;
    g_appLogDebug.count = app_log_count;
    g_appLogDebug.head_index = app_log_head_index;
    g_appLogDebug.tail_index = app_log_tail_index;
    g_appLogDebug.last_event_code = record->event_code;
}

static uint8_t App_Log_EnableStorage(void)
{
#if defined(APP_LOG_HOST_TEST)
    return 1U;
#else
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    return (HAL_PWREx_EnableBkUpReg() == HAL_OK) ? 1U : 0U;
#endif
}

static uint8_t App_Log_IsSequenceNewer(uint32_t candidate,
                                      uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1U : 0U;
}

static uint32_t App_Log_PreviousSequence(uint32_t sequence)
{
    return (sequence == 1U) ? UINT32_MAX : (sequence - 1U);
}

static void App_Log_CleanRetainedRange(void *address, int32_t size)
{
#if defined(APP_LOG_HOST_TEST)
    (void)address;
    (void)size;
#else
    SCB_CleanDCache_by_Addr((uint32_t *)address, size);
    __DSB();
#endif
}

static void App_Log_ClearRetainedStorage(void)
{
    uint16_t slot_index;
    uint8_t word_index;

    for (slot_index = 0U;
         slot_index < APP_LOG_RAM_CAPACITY;
         slot_index++)
    {
        for (word_index = 0U;
             word_index <
                (sizeof(App_Log_Record_t) / sizeof(uint32_t));
             word_index++)
        {
            g_appLogRecords[slot_index].words[word_index] = 0U;
        }
    }

    App_Log_CleanRetainedRange(
        g_appLogRecords, (int32_t)sizeof(g_appLogRecords));
}

static void App_Log_CommitRecord(
    uint16_t index,
    const App_Log_Record_t *record)
{
    App_Log_StorageSlot_t source;
    uint8_t word_index;

    source.record = *record;

    /*
     * Backup SRAM uses 32-bit ECC words and is cacheable. Invalidate and
     * commit with aligned full-word stores, then clean the complete cache line.
     */
    g_appLogRecords[index].words[
        (sizeof(App_Log_Record_t) / sizeof(uint32_t)) - 1U] = 0U;
#if !defined(APP_LOG_HOST_TEST)
    __DMB();
#endif

    for (word_index = 0U;
         word_index <
            ((sizeof(App_Log_Record_t) / sizeof(uint32_t)) - 1U);
         word_index++)
    {
        g_appLogRecords[index].words[word_index] =
            source.words[word_index];
    }

    g_appLogRecords[index].words[
        (sizeof(App_Log_Record_t) / sizeof(uint32_t)) - 1U] =
        source.words[
            (sizeof(App_Log_Record_t) / sizeof(uint32_t)) - 1U];

    App_Log_CleanRetainedRange(
        &g_appLogRecords[index], (int32_t)sizeof(g_appLogRecords[index]));
}

static void App_Log_Recover(void)
{
    uint16_t index;
    uint16_t newest_index = 0U;
    uint16_t recovered_count = 0U;
    uint16_t invalid_count = 0U;
    uint32_t newest_sequence = 0U;
    uint32_t expected_sequence;
    uint8_t found = 0U;

    for (index = 0U; index < APP_LOG_RAM_CAPACITY; index++)
    {
        if (App_Log_IsRecordValid(
                &g_appLogRecords[index].record) == 0U)
        {
            if (g_appLogRecords[index].record.commit_marker ==
                APP_LOG_COMMIT_MARKER)
            {
                invalid_count++;
            }
            continue;
        }

        if ((found == 0U) ||
            (App_Log_IsSequenceNewer(
                g_appLogRecords[index].record.sequence,
                newest_sequence) != 0U))
        {
            newest_sequence = g_appLogRecords[index].record.sequence;
            newest_index = index;
            found = 1U;
        }
    }

    if (found == 0U)
    {
        App_Log_ClearRetainedStorage();
        app_log_head_index = 0U;
        app_log_tail_index = 0U;
        app_log_count = 0U;
        app_log_next_sequence = 1U;
    }
    else
    {
        index = newest_index;
        expected_sequence = newest_sequence;

        while ((recovered_count < APP_LOG_RAM_CAPACITY) &&
               (App_Log_IsRecordValid(
                    &g_appLogRecords[index].record) != 0U) &&
               (g_appLogRecords[index].record.sequence ==
                expected_sequence))
        {
            recovered_count++;
            expected_sequence =
                App_Log_PreviousSequence(expected_sequence);
            index = (uint16_t)(
                (index + APP_LOG_RAM_CAPACITY - 1U) %
                APP_LOG_RAM_CAPACITY);
        }

        app_log_count = recovered_count;
        app_log_head_index = (uint16_t)(
            (newest_index + 1U) % APP_LOG_RAM_CAPACITY);
        app_log_tail_index = (uint16_t)(
            (app_log_head_index + APP_LOG_RAM_CAPACITY -
             app_log_count) % APP_LOG_RAM_CAPACITY);
        app_log_next_sequence = newest_sequence + 1U;
        if (app_log_next_sequence == 0U)
        {
            app_log_next_sequence = 1U;
        }
    }

    g_appLogDebug.recovered_count = recovered_count;
    g_appLogDebug.invalid_retained_count = invalid_count;
    g_appLogDebug.write_count = recovered_count;
    g_appLogDebug.next_sequence = app_log_next_sequence;
    g_appLogDebug.count = app_log_count;
    g_appLogDebug.head_index = app_log_head_index;
    g_appLogDebug.tail_index = app_log_tail_index;
    if (found != 0U)
    {
        g_appLogDebug.last_sequence = newest_sequence;
        g_appLogDebug.last_event_code =
            g_appLogRecords[newest_index].record.event_code;
    }
}

void App_Log_Init(void)
{
    uint8_t storage_ready = App_Log_EnableStorage();
    uint32_t primask = App_Log_EnterCritical();

    g_appLogDebug = (App_Log_Debug_t){0};
    g_appLogDebug.storage_ready = storage_ready;

    if (g_appLogDebug.storage_ready != 0U)
    {
        App_Log_Recover();
    }
    else
    {
        app_log_head_index = 0U;
        app_log_tail_index = 0U;
        app_log_count = 0U;
        app_log_next_sequence = 1U;
        g_appLogDebug.next_sequence = app_log_next_sequence;
    }

    App_Log_ExitCritical(primask);
}

uint8_t App_Log_Push(App_Log_Source_t source,
                     App_Log_Severity_t severity,
                     App_Log_EventCode_t event_code,
                     uint32_t data_0,
                     uint32_t data_1)
{
    return App_Log_PushWithRtcTime(source,
                                   severity,
                                   event_code,
                                   0U,
                                   data_0,
                                   data_1);
}

uint8_t App_Log_PushWithRtcTime(App_Log_Source_t source,
                                App_Log_Severity_t severity,
                                App_Log_EventCode_t event_code,
                                uint32_t rtc_epoch_s,
                                uint32_t data_0,
                                uint32_t data_1)
{
    App_Log_Record_t record = {0};
    uint32_t primask;

    if (App_Log_IsInputValid(source, severity) == 0U)
    {
        primask = App_Log_EnterCritical();
        g_appLogDebug.rejected_count++;
        App_Log_ExitCritical(primask);
        return 0U;
    }

    if (g_appLogDebug.storage_ready == 0U)
    {
        primask = App_Log_EnterCritical();
        g_appLogDebug.rejected_count++;
        App_Log_ExitCritical(primask);
        return 0U;
    }

    record.magic = APP_LOG_RECORD_MAGIC;
    record.uptime_ms = HAL_GetTick();
    record.rtc_epoch_s = rtc_epoch_s;
    record.event_code = (uint16_t)event_code;
    record.source = (uint8_t)source;
    record.severity = (uint8_t)severity;
    record.data_0 = data_0;
    record.data_1 = data_1;

    primask = App_Log_EnterCritical();

    record.sequence = app_log_next_sequence;
    app_log_next_sequence++;

    if (app_log_next_sequence == 0U)
    {
        app_log_next_sequence = 1U;
    }

    record.crc16 = App_Log_CalculateCrc16(
        (const uint8_t *)&record,
        (uint32_t)offsetof(App_Log_Record_t, crc16));
    record.commit_marker = APP_LOG_COMMIT_MARKER;
    App_Log_CommitRecord(app_log_head_index, &record);
    app_log_head_index = (uint16_t)(
        (app_log_head_index + 1U) % APP_LOG_RAM_CAPACITY);

    if (app_log_count < APP_LOG_RAM_CAPACITY)
    {
        app_log_count++;
    }
    else
    {
        app_log_tail_index = (uint16_t)(
            (app_log_tail_index + 1U) % APP_LOG_RAM_CAPACITY);
        g_appLogDebug.overwrite_count++;
    }

    App_Log_UpdateDebugAfterWrite(&record);
    App_Log_ExitCritical(primask);
    return 1U;
}

uint16_t App_Log_GetCount(void)
{
    uint16_t count;
    uint32_t primask = App_Log_EnterCritical();

    count = app_log_count;

    App_Log_ExitCritical(primask);
    return count;
}

uint8_t App_Log_ReadOldest(uint16_t age_index,
                          App_Log_Record_t *record)
{
    uint16_t physical_index;
    uint32_t primask;

    if (record == NULL)
    {
        return 0U;
    }

    primask = App_Log_EnterCritical();

    if (age_index >= app_log_count)
    {
        App_Log_ExitCritical(primask);
        return 0U;
    }

    physical_index = (uint16_t)(
        (app_log_tail_index + age_index) % APP_LOG_RAM_CAPACITY);
    *record = g_appLogRecords[physical_index].record;

    App_Log_ExitCritical(primask);
    return 1U;
}

uint8_t App_Log_ReadBySequence(uint32_t sequence,
                              App_Log_Record_t *record)
{
    uint16_t age_index;
    uint16_t physical_index;
    uint32_t primask;

    if ((record == NULL) || (sequence == 0U))
    {
        return 0U;
    }

    primask = App_Log_EnterCritical();

    for (age_index = 0U; age_index < app_log_count; age_index++)
    {
        physical_index = (uint16_t)(
            (app_log_tail_index + age_index) % APP_LOG_RAM_CAPACITY);

        if (g_appLogRecords[physical_index].record.sequence == sequence)
        {
            *record = g_appLogRecords[physical_index].record;
            App_Log_ExitCritical(primask);
            return 1U;
        }
    }

    App_Log_ExitCritical(primask);
    return 0U;
}

uint8_t App_Log_GetLatest(App_Log_Record_t *record)
{
    uint16_t physical_index;
    uint32_t primask;

    if (record == NULL)
    {
        return 0U;
    }

    primask = App_Log_EnterCritical();

    if (app_log_count == 0U)
    {
        App_Log_ExitCritical(primask);
        return 0U;
    }

    physical_index = (uint16_t)(
        (app_log_head_index + APP_LOG_RAM_CAPACITY - 1U) %
        APP_LOG_RAM_CAPACITY);
    *record = g_appLogRecords[physical_index].record;

    App_Log_ExitCritical(primask);
    return 1U;
}

uint8_t App_Log_IsRecordValid(const App_Log_Record_t *record)
{
    uint16_t calculated_crc;

    if ((record == NULL) ||
        (record->magic != APP_LOG_RECORD_MAGIC) ||
        (record->commit_marker != APP_LOG_COMMIT_MARKER))
    {
        return 0U;
    }

    calculated_crc = App_Log_CalculateCrc16(
        (const uint8_t *)record,
        (uint32_t)offsetof(App_Log_Record_t, crc16));

    return (calculated_crc == record->crc16) ? 1U : 0U;
}

void App_Log_GetInfo(App_Log_Info_t *info)
{
    uint16_t latest_index;
    uint32_t primask;

    if (info == NULL)
    {
        return;
    }

    primask = App_Log_EnterCritical();

    info->write_count = g_appLogDebug.write_count;
    info->overwrite_count = g_appLogDebug.overwrite_count;
    info->count = app_log_count;
    info->capacity = APP_LOG_RAM_CAPACITY;
    info->record_size = (uint16_t)sizeof(App_Log_Record_t);

    if (app_log_count == 0U)
    {
        info->oldest_sequence = 0U;
        info->latest_sequence = 0U;
    }
    else
    {
        latest_index = (uint16_t)(
            (app_log_head_index + APP_LOG_RAM_CAPACITY - 1U) %
            APP_LOG_RAM_CAPACITY);
        info->oldest_sequence =
            g_appLogRecords[app_log_tail_index].record.sequence;
        info->latest_sequence =
            g_appLogRecords[latest_index].record.sequence;
    }

    App_Log_ExitCritical(primask);
}

void App_Log_GetDebug(App_Log_Debug_t *debug)
{
    uint32_t primask;

    if (debug == NULL)
    {
        return;
    }

    primask = App_Log_EnterCritical();

    debug->write_count = g_appLogDebug.write_count;
    debug->overwrite_count = g_appLogDebug.overwrite_count;
    debug->rejected_count = g_appLogDebug.rejected_count;
    debug->next_sequence = g_appLogDebug.next_sequence;
    debug->last_sequence = g_appLogDebug.last_sequence;
    debug->count = g_appLogDebug.count;
    debug->head_index = g_appLogDebug.head_index;
    debug->tail_index = g_appLogDebug.tail_index;
    debug->last_event_code = g_appLogDebug.last_event_code;
    debug->recovered_count = g_appLogDebug.recovered_count;
    debug->invalid_retained_count =
        g_appLogDebug.invalid_retained_count;
    debug->storage_ready = g_appLogDebug.storage_ready;

    App_Log_ExitCritical(primask);
}

void App_Log_Clear(void)
{
    uint32_t primask = App_Log_EnterCritical();
    uint8_t storage_ready = g_appLogDebug.storage_ready;

    app_log_head_index = 0U;
    app_log_tail_index = 0U;
    app_log_count = 0U;
    app_log_next_sequence = 1U;
    if (storage_ready != 0U)
    {
        App_Log_ClearRetainedStorage();
    }
    g_appLogDebug = (App_Log_Debug_t){0};
    g_appLogDebug.storage_ready = storage_ready;
    g_appLogDebug.next_sequence = app_log_next_sequence;

    App_Log_ExitCritical(primask);
}
