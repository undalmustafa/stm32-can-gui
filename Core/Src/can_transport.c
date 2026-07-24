#include "can_transport.h"

typedef struct
{
    uint32_t identifier;
    CAN_Transport_IdType_t id_type;
    uint8_t data[8];
    uint8_t high_priority;
    uint8_t replaceable;
} CAN_Transport_Message_t;

static FDCAN_HandleTypeDef *can_handle = NULL;
static CAN_Transport_Message_t tx_queue[CAN_TRANSPORT_TX_QUEUE_CAPACITY];
static uint8_t tx_queue_count = 0U;
static CAN_Transport_Stats_t transport_stats;
static uint32_t queue_pending_since_tick = 0U;
static uint8_t queue_pending_timer_active = 0U;
static uint8_t queue_stuck_reported = 0U;

static void CAN_Transport_CopyData(uint8_t destination[8],
                                   const uint8_t source[8])
{
    uint8_t index;

    for (index = 0U; index < 8U; index++)
    {
        destination[index] = source[index];
    }
}

static void CAN_Transport_UpdateQueueHealth(void)
{
    uint32_t now;
    uint32_t pending_duration;

    if (tx_queue_count == 0U)
    {
        if (queue_pending_timer_active != 0U)
        {
            pending_duration = HAL_GetTick() - queue_pending_since_tick;
            if (pending_duration > transport_stats.longest_pending_ms)
            {
                transport_stats.longest_pending_ms = pending_duration;
            }
        }

        queue_pending_timer_active = 0U;
        queue_stuck_reported = 0U;
        transport_stats.current_pending_ms = 0U;
        transport_stats.queue_stuck_active = 0U;
        return;
    }

    now = HAL_GetTick();

    if (queue_pending_timer_active == 0U)
    {
        queue_pending_since_tick = now;
        queue_pending_timer_active = 1U;
        queue_stuck_reported = 0U;
    }

    pending_duration = now - queue_pending_since_tick;
    transport_stats.current_pending_ms = pending_duration;

    if (pending_duration > transport_stats.longest_pending_ms)
    {
        transport_stats.longest_pending_ms = pending_duration;
    }

    if ((pending_duration >= CAN_TRANSPORT_QUEUE_STUCK_TIMEOUT_MS) &&
        (queue_stuck_reported == 0U))
    {
        queue_stuck_reported = 1U;
        transport_stats.queue_stuck_active = 1U;
        transport_stats.queue_stuck_events++;
    }
}

static CAN_Transport_Result_t CAN_Transport_TryHardwareSend(
    CAN_Transport_Message_t *message)
{
    FDCAN_TxHeaderTypeDef tx_header = {0};

    if (HAL_FDCAN_GetTxFifoFreeLevel(can_handle) == 0U)
    {
        return CAN_TRANSPORT_QUEUE_FULL;
    }

    tx_header.Identifier = message->identifier;
    tx_header.IdType = (message->id_type == CAN_TRANSPORT_ID_STANDARD)
                     ? FDCAN_STANDARD_ID
                     : FDCAN_EXTENDED_ID;
    tx_header.TxFrameType = FDCAN_DATA_FRAME;
    tx_header.DataLength = FDCAN_DLC_BYTES_8;
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_OFF;
    tx_header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_header.MessageMarker = 0U;

    if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle,
                                      &tx_header,
                                      message->data) != HAL_OK)
    {
        return CAN_TRANSPORT_HAL_ERROR;
    }

    return CAN_TRANSPORT_OK;
}

static CAN_Transport_Result_t CAN_Transport_Enqueue(
    const CAN_Transport_Message_t *message)
{
    uint8_t insert_index;
    uint8_t move_index;
    uint8_t evict_index;
    uint8_t queue_was_empty;

    queue_was_empty = (tx_queue_count == 0U) ? 1U : 0U;

    if (tx_queue_count >= CAN_TRANSPORT_TX_QUEUE_CAPACITY)
    {
        if (message->high_priority == 0U)
        {
            transport_stats.queue_overflow++;
            return CAN_TRANSPORT_QUEUE_FULL;
        }

        evict_index = tx_queue_count;
        while (evict_index > 0U)
        {
            evict_index--;
            if (tx_queue[evict_index].replaceable != 0U)
            {
                break;
            }
        }

        if (tx_queue[evict_index].replaceable == 0U)
        {
            transport_stats.queue_overflow++;
            return CAN_TRANSPORT_QUEUE_FULL;
        }

        for (move_index = evict_index;
             (uint8_t)(move_index + 1U) < tx_queue_count;
             move_index++)
        {
            tx_queue[move_index] = tx_queue[move_index + 1U];
        }

        tx_queue_count--;
        transport_stats.periodic_evicted++;
    }

    insert_index = tx_queue_count;
    if (message->high_priority != 0U)
    {
        insert_index = 0U;
        while ((insert_index < tx_queue_count) &&
               (tx_queue[insert_index].high_priority != 0U))
        {
            insert_index++;
        }
    }

    move_index = tx_queue_count;
    while (move_index > insert_index)
    {
        tx_queue[move_index] = tx_queue[move_index - 1U];
        move_index--;
    }

    tx_queue[insert_index] = *message;
    tx_queue_count++;
    transport_stats.enqueued++;

    if (queue_was_empty != 0U)
    {
        queue_pending_since_tick = HAL_GetTick();
        queue_pending_timer_active = 1U;
        queue_stuck_reported = 0U;
        transport_stats.current_pending_ms = 0U;
        transport_stats.queue_stuck_active = 0U;
    }

    if (message->high_priority != 0U)
    {
        transport_stats.high_priority_enqueued++;
    }

    if (tx_queue_count > transport_stats.queue_high_watermark)
    {
        transport_stats.queue_high_watermark = tx_queue_count;
    }

    return CAN_TRANSPORT_QUEUED;
}

static CAN_Transport_Message_t *CAN_Transport_FindPending(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type)
{
    uint8_t queue_index;

    for (queue_index = 0U; queue_index < tx_queue_count; queue_index++)
    {
        if ((tx_queue[queue_index].identifier == identifier) &&
            (tx_queue[queue_index].id_type == id_type) &&
            (tx_queue[queue_index].replaceable != 0U))
        {
            return &tx_queue[queue_index];
        }
    }

    return NULL;
}

static CAN_Transport_Result_t CAN_Transport_SendClassicInternal(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8],
    uint8_t replace_pending,
    uint8_t high_priority)
{
    CAN_Transport_Message_t message;
    CAN_Transport_Message_t *pending_message;
    CAN_Transport_Result_t result;

    if (can_handle == NULL)
    {
        return CAN_TRANSPORT_NOT_INITIALIZED;
    }

    if (data == NULL)
    {
        return CAN_TRANSPORT_INVALID_ARGUMENT;
    }

    if (id_type == CAN_TRANSPORT_ID_STANDARD)
    {
        if (identifier > 0x7FFU)
        {
            return CAN_TRANSPORT_INVALID_ID;
        }
    }
    else if (id_type == CAN_TRANSPORT_ID_EXTENDED)
    {
        if (identifier > 0x1FFFFFFFUL)
        {
            return CAN_TRANSPORT_INVALID_ID;
        }
    }
    else
    {
        return CAN_TRANSPORT_INVALID_ARGUMENT;
    }

    CAN_Transport_UpdateQueueHealth();

    message.identifier = identifier;
    message.id_type = id_type;
    message.high_priority = high_priority;
    message.replaceable = replace_pending;
    CAN_Transport_CopyData(message.data, data);

    if (tx_queue_count == 0U)
    {
        result = CAN_Transport_TryHardwareSend(&message);

        if (result == CAN_TRANSPORT_OK)
        {
            transport_stats.sent_direct++;
            return CAN_TRANSPORT_OK;
        }

        if (result == CAN_TRANSPORT_HAL_ERROR)
        {
            transport_stats.hal_error++;
        }
    }

    if (replace_pending != 0U)
    {
        pending_message = CAN_Transport_FindPending(identifier, id_type);
        if (pending_message != NULL)
        {
            CAN_Transport_CopyData(pending_message->data, data);
            transport_stats.coalesced++;
            return CAN_TRANSPORT_QUEUED;
        }
    }

    return CAN_Transport_Enqueue(&message);
}

void CAN_Transport_Init(FDCAN_HandleTypeDef *hfdcan)
{
    can_handle = hfdcan;
    tx_queue_count = 0U;

    transport_stats.sent_direct = 0U;
    transport_stats.enqueued = 0U;
    transport_stats.coalesced = 0U;
    transport_stats.high_priority_enqueued = 0U;
    transport_stats.periodic_evicted = 0U;
    transport_stats.sent_from_queue = 0U;
    transport_stats.queue_overflow = 0U;
    transport_stats.hal_error = 0U;
    transport_stats.queue_stuck_events = 0U;
    transport_stats.current_pending_ms = 0U;
    transport_stats.longest_pending_ms = 0U;
    transport_stats.pending_count = 0U;
    transport_stats.queue_high_watermark = 0U;
    transport_stats.queue_stuck_active = 0U;

    queue_pending_since_tick = 0U;
    queue_pending_timer_active = 0U;
    queue_stuck_reported = 0U;
}

CAN_Transport_Result_t CAN_Transport_SendClassic(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8])
{
    return CAN_Transport_SendClassicInternal(identifier,
                                             id_type,
                                             data,
                                             0U,
                                             0U);
}

CAN_Transport_Result_t CAN_Transport_SendClassicLatest(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8])
{
    return CAN_Transport_SendClassicInternal(identifier,
                                             id_type,
                                             data,
                                             1U,
                                             0U);
}

CAN_Transport_Result_t CAN_Transport_SendClassicHighPriority(
    uint32_t identifier,
    CAN_Transport_IdType_t id_type,
    const uint8_t data[8])
{
    return CAN_Transport_SendClassicInternal(identifier,
                                             id_type,
                                             data,
                                             0U,
                                             1U);
}

void CAN_Transport_Process(void)
{
    CAN_Transport_Result_t result;
    uint8_t move_index;

    if (can_handle == NULL)
    {
        return;
    }

    CAN_Transport_UpdateQueueHealth();

    while (tx_queue_count > 0U)
    {
        result = CAN_Transport_TryHardwareSend(&tx_queue[0]);

        if (result != CAN_TRANSPORT_OK)
        {
            if (result == CAN_TRANSPORT_HAL_ERROR)
            {
                transport_stats.hal_error++;
            }
            break;
        }

        tx_queue_count--;
        for (move_index = 0U; move_index < tx_queue_count; move_index++)
        {
            tx_queue[move_index] = tx_queue[move_index + 1U];
        }

        transport_stats.sent_from_queue++;
    }

    CAN_Transport_UpdateQueueHealth();
}

void CAN_Transport_GetStats(CAN_Transport_Stats_t *stats)
{
    if (stats != NULL)
    {
        CAN_Transport_UpdateQueueHealth();
        *stats = transport_stats;
        stats->pending_count = tx_queue_count;
    }
}
