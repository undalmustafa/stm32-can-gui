#include "app_timing.h"

#include "pca2131.h"
#include "tic12400.h"

#include <stddef.h>
#include <stdint.h>

#if !defined(APP_TIMING_HOST_TEST)
#include "main.h"
#endif

#define APP_TIMING_TIC12400_BUDGET_US       12000UL
#define APP_TIMING_CONTROL_BUDGET_US          2000UL
#define APP_TIMING_RTC_BUDGET_US            12000UL
#define APP_TIMING_INPUT_CAPTURE_BUDGET_US    1000UL
#define APP_TIMING_CAN_APP_BUDGET_US         25000UL
#define APP_TIMING_TIC12400_CAN_BUDGET_US     5000UL
#define APP_TIMING_WATCHDOG_BUDGET_US         1000UL
#define APP_TIMING_MAIN_LOOP_BUDGET_US       50000UL

#define APP_TIMING_ACK_FINITE_BIN_COUNT \
    (APP_TIMING_ACK_BIN_COUNT - 1U)

_Static_assert(APP_TIMING_TIC12400_BUDGET_US >=
               (TIC12400_SPI_TIMEOUT_MS * 1000UL),
               "TIC12400 service budget is below its HAL timeout");
_Static_assert(APP_TIMING_RTC_BUDGET_US >=
               (PCA2131_I2C_TIMEOUT_MS * 1000UL),
               "RTC service budget is below its HAL timeout");

static const uint32_t service_budget_us[APP_TIMING_SERVICE_COUNT] =
{
    APP_TIMING_MAIN_LOOP_BUDGET_US,
    APP_TIMING_TIC12400_BUDGET_US,
    APP_TIMING_CONTROL_BUDGET_US,
    APP_TIMING_RTC_BUDGET_US,
    APP_TIMING_INPUT_CAPTURE_BUDGET_US,
    APP_TIMING_CAN_APP_BUDGET_US,
    APP_TIMING_TIC12400_CAN_BUDGET_US,
    APP_TIMING_WATCHDOG_BUDGET_US
};

static const uint32_t ack_bin_upper_us[APP_TIMING_ACK_FINITE_BIN_COUNT] =
{
    100U,
    250U,
    500U,
    1000U,
    2000U,
    5000U,
    10000U,
    25000U
};

static volatile App_Timing_Snapshot_t g_appTiming;

static uint32_t App_Timing_MicrosecondsToCycles(uint32_t microseconds)
{
    uint64_t cycles =
        ((uint64_t)g_appTiming.core_clock_hz * microseconds + 999999ULL) /
        1000000ULL;

    return (cycles > UINT32_MAX) ? UINT32_MAX : (uint32_t)cycles;
}

uint32_t App_Timing_CyclesToMicroseconds(uint32_t cycles)
{
    uint64_t microseconds;

    if (g_appTiming.core_clock_hz == 0U)
    {
        return 0U;
    }

    microseconds =
        ((uint64_t)cycles * 1000000ULL +
         g_appTiming.core_clock_hz - 1U) /
        g_appTiming.core_clock_hz;
    return (microseconds > UINT32_MAX)
        ? UINT32_MAX
        : (uint32_t)microseconds;
}

void App_Timing_Init(uint32_t core_clock_hz)
{
    uint32_t service;

    g_appTiming = (App_Timing_Snapshot_t){0};
    g_appTiming.core_clock_hz = core_clock_hz;
    g_appTiming.init_count = 1U;

    for (service = 0U;
         service < (uint32_t)APP_TIMING_SERVICE_COUNT;
         service++)
    {
        g_appTiming.service[service].minimum_cycles = UINT32_MAX;
        g_appTiming.service[service].budget_cycles =
            App_Timing_MicrosecondsToCycles(service_budget_us[service]);
    }
    g_appTiming.ack.minimum_cycles = UINT32_MAX;

#if defined(APP_TIMING_HOST_TEST)
    g_appTiming.enabled = (core_clock_hz != 0U) ? 1U : 0U;
#elif defined(DWT) && defined(CoreDebug) && \
      defined(DWT_CTRL_CYCCNTENA_Msk)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55UL;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_appTiming.enabled =
        ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;
#else
    g_appTiming.enabled = 0U;
#endif
}

uint32_t App_Timing_Now(void)
{
#if defined(APP_TIMING_HOST_TEST)
    return 0U;
#elif defined(DWT) && defined(DWT_CTRL_CYCCNTENA_Msk)
    return DWT->CYCCNT;
#else
    return 0U;
#endif
}

uint32_t App_Timing_Begin(void)
{
    return App_Timing_Now();
}

static void App_Timing_RecordServiceCycles(
    App_Timing_Service_t service,
    uint32_t elapsed_cycles)
{
    volatile App_Timing_ServiceStats_t *stats;

    if ((g_appTiming.enabled == 0U) ||
        ((uint32_t)service >= (uint32_t)APP_TIMING_SERVICE_COUNT))
    {
        return;
    }

    stats = &g_appTiming.service[service];
    stats->sample_count++;
    stats->current_cycles = elapsed_cycles;
    if (elapsed_cycles < stats->minimum_cycles)
    {
        stats->minimum_cycles = elapsed_cycles;
    }
    if (elapsed_cycles > stats->maximum_cycles)
    {
        stats->maximum_cycles = elapsed_cycles;
    }
    if (elapsed_cycles > stats->budget_cycles)
    {
        stats->overrun_count++;
    }
}

void App_Timing_RecordElapsed(App_Timing_Service_t service,
                              uint32_t start_cycles,
                              uint32_t end_cycles)
{
    App_Timing_RecordServiceCycles(
        service,
        (uint32_t)(end_cycles - start_cycles));
}

void App_Timing_End(App_Timing_Service_t service, uint32_t start_cycles)
{
    App_Timing_RecordElapsed(service, start_cycles, App_Timing_Now());
}

void App_Timing_RecordAckElapsed(uint32_t start_cycles,
                                 uint32_t end_cycles)
{
    uint32_t elapsed_cycles;
    uint32_t elapsed_us;
    uint32_t bin;
    volatile App_Timing_AckStats_t *stats = &g_appTiming.ack;

    if (g_appTiming.enabled == 0U)
    {
        return;
    }

    elapsed_cycles = (uint32_t)(end_cycles - start_cycles);
    elapsed_us = App_Timing_CyclesToMicroseconds(elapsed_cycles);
    stats->sample_count++;
    stats->current_cycles = elapsed_cycles;
    if (elapsed_cycles < stats->minimum_cycles)
    {
        stats->minimum_cycles = elapsed_cycles;
    }
    if (elapsed_cycles > stats->maximum_cycles)
    {
        stats->maximum_cycles = elapsed_cycles;
    }

    for (bin = 0U; bin < APP_TIMING_ACK_FINITE_BIN_COUNT; bin++)
    {
        if (elapsed_us <= ack_bin_upper_us[bin])
        {
            stats->bins[bin]++;
            return;
        }
    }

    stats->bins[APP_TIMING_ACK_BIN_COUNT - 1U]++;
}

static uint32_t App_Timing_GetPercentileUs(uint32_t percentile)
{
    uint64_t scaled_rank;
    uint32_t rank;
    uint32_t cumulative = 0U;
    uint32_t bin;

    if (g_appTiming.ack.sample_count == 0U)
    {
        return 0U;
    }

    scaled_rank =
        (uint64_t)g_appTiming.ack.sample_count * percentile;
    rank = (uint32_t)((scaled_rank + 99ULL) / 100ULL);

    for (bin = 0U; bin < APP_TIMING_ACK_BIN_COUNT; bin++)
    {
        cumulative += g_appTiming.ack.bins[bin];
        if (cumulative >= rank)
        {
            if (bin < APP_TIMING_ACK_FINITE_BIN_COUNT)
            {
                return ack_bin_upper_us[bin];
            }
            return App_Timing_CyclesToMicroseconds(
                g_appTiming.ack.maximum_cycles);
        }
    }

    return App_Timing_CyclesToMicroseconds(
        g_appTiming.ack.maximum_cycles);
}

void App_Timing_GetSnapshot(App_Timing_Snapshot_t *snapshot)
{
    uint32_t service;
    uint32_t bin;

    if (snapshot == NULL)
    {
        return;
    }

    snapshot->core_clock_hz = g_appTiming.core_clock_hz;
    snapshot->init_count = g_appTiming.init_count;
    snapshot->enabled = g_appTiming.enabled;
    for (service = 0U;
         service < (uint32_t)APP_TIMING_SERVICE_COUNT;
         service++)
    {
        snapshot->service[service].sample_count =
            g_appTiming.service[service].sample_count;
        snapshot->service[service].current_cycles =
            g_appTiming.service[service].current_cycles;
        snapshot->service[service].minimum_cycles =
            (g_appTiming.service[service].sample_count == 0U)
            ? 0U
            : g_appTiming.service[service].minimum_cycles;
        snapshot->service[service].maximum_cycles =
            g_appTiming.service[service].maximum_cycles;
        snapshot->service[service].budget_cycles =
            g_appTiming.service[service].budget_cycles;
        snapshot->service[service].overrun_count =
            g_appTiming.service[service].overrun_count;
    }
    snapshot->ack.sample_count = g_appTiming.ack.sample_count;
    snapshot->ack.current_cycles = g_appTiming.ack.current_cycles;
    snapshot->ack.minimum_cycles =
        (g_appTiming.ack.sample_count == 0U)
        ? 0U
        : g_appTiming.ack.minimum_cycles;
    snapshot->ack.maximum_cycles = g_appTiming.ack.maximum_cycles;
    for (bin = 0U; bin < APP_TIMING_ACK_BIN_COUNT; bin++)
    {
        snapshot->ack.bins[bin] = g_appTiming.ack.bins[bin];
    }
}

void App_Timing_GetAckSummary(App_Timing_AckSummary_t *summary)
{
    if (summary == NULL)
    {
        return;
    }

    summary->sample_count = g_appTiming.ack.sample_count;
    summary->current_us = App_Timing_CyclesToMicroseconds(
        g_appTiming.ack.current_cycles);
    summary->p50_us = App_Timing_GetPercentileUs(50U);
    summary->p95_us = App_Timing_GetPercentileUs(95U);
    summary->p99_us = App_Timing_GetPercentileUs(99U);
    summary->maximum_us = App_Timing_CyclesToMicroseconds(
        g_appTiming.ack.maximum_cycles);
}
