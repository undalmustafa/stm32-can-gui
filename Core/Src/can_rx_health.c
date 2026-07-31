#include "can_rx_health.h"

#include <stddef.h>

static volatile CAN_RxHealth_Stats_t rx_health_stats;

void CAN_RxHealth_Init(void)
{
    rx_health_stats.new_message_events = 0U;
    rx_health_stats.watermark_events = 0U;
    rx_health_stats.full_events = 0U;
    rx_health_stats.message_lost_events = 0U;
    rx_health_stats.max_fill_level = 0U;
}

void CAN_RxHealth_RecordIsr(uint32_t events, uint32_t fill_level)
{
    if ((events & CAN_RX_HEALTH_EVENT_NEW_MESSAGE) != 0U)
    {
        rx_health_stats.new_message_events++;
    }

    if ((events & CAN_RX_HEALTH_EVENT_WATERMARK) != 0U)
    {
        rx_health_stats.watermark_events++;
    }

    if ((events & CAN_RX_HEALTH_EVENT_FULL) != 0U)
    {
        rx_health_stats.full_events++;
    }

    if ((events & CAN_RX_HEALTH_EVENT_MESSAGE_LOST) != 0U)
    {
        rx_health_stats.message_lost_events++;
    }

    if (fill_level > rx_health_stats.max_fill_level)
    {
        rx_health_stats.max_fill_level = fill_level;
    }
}

void CAN_RxHealth_GetStats(CAN_RxHealth_Stats_t *stats)
{
    if (stats != NULL)
    {
        stats->new_message_events = rx_health_stats.new_message_events;
        stats->watermark_events = rx_health_stats.watermark_events;
        stats->full_events = rx_health_stats.full_events;
        stats->message_lost_events = rx_health_stats.message_lost_events;
        stats->max_fill_level = rx_health_stats.max_fill_level;
    }
}
