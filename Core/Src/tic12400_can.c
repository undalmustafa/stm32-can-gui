#include "tic12400_can.h"

#include "can_protocol.h"
#include "tic12400_probe.h"
#include "tic12400_switch.h"

#define TIC12400_CAN_STATUS_PERIOD_MS 500U
#define TIC12400_CAN_SWITCH_PERIOD_MS 500U
#define TIC12400_CAN_PROFILE_PERIOD_MS 500U

_Static_assert(
    CAN_PROTOCOL_TIC12400_CHANNEL_COUNT ==
    TIC12400_SWITCH_CHANNEL_COUNT,
    "TIC12400 protocol and driver channel counts must match");
_Static_assert(
    CAN_PROTOCOL_TIC12400_BATTERY_CAPABLE_MASK ==
    TIC12400_PROFILE_BATTERY_CAPABLE_MASK,
    "TIC12400 battery-capable masks must match");

static volatile TIC12400_CanSnapshot_t g_tic12400_can;

static uint32_t tic12400_can_last_status_tick;
static uint32_t tic12400_can_last_switch_tick;
static uint32_t tic12400_can_last_profile_tick;

static uint16_t TIC12400_CAN_SaturateU16(uint32_t value)
{
    return (value > 0xFFFFU) ? 0xFFFFU : (uint16_t)value;
}

static uint8_t TIC12400_CAN_MapResult(TIC12400_Result_t result)
{
    switch (result)
    {
        case TIC12400_RESULT_OK:
            return CAN_PROTOCOL_TIC12400_RESULT_OK;
        case TIC12400_RESULT_INVALID_ARGUMENT:
            return CAN_PROTOCOL_TIC12400_RESULT_INVALID_ARGUMENT;
        case TIC12400_RESULT_INVALID_ADDRESS:
            return CAN_PROTOCOL_TIC12400_RESULT_INVALID_ADDRESS;
        case TIC12400_RESULT_HAL_ERROR:
            return CAN_PROTOCOL_TIC12400_RESULT_HAL_ERROR;
        case TIC12400_RESULT_RESPONSE_PARITY_ERROR:
            return CAN_PROTOCOL_TIC12400_RESULT_RESPONSE_PARITY_ERROR;
        case TIC12400_RESULT_DEVICE_SPI_ERROR:
            return CAN_PROTOCOL_TIC12400_RESULT_DEVICE_SPI_ERROR;
        case TIC12400_RESULT_DEVICE_PARITY_ERROR:
            return CAN_PROTOCOL_TIC12400_RESULT_DEVICE_PARITY_ERROR;
        case TIC12400_RESULT_DEVICE_ID_MISMATCH:
            return CAN_PROTOCOL_TIC12400_RESULT_DEVICE_ID_MISMATCH;
        case TIC12400_RESULT_INVALID_DATA:
            return CAN_PROTOCOL_TIC12400_RESULT_INVALID_DATA;
        case TIC12400_RESULT_REGISTER_VERIFY_MISMATCH:
            return CAN_PROTOCOL_TIC12400_RESULT_REGISTER_VERIFY_MISMATCH;
        case TIC12400_RESULT_CRC_TIMEOUT:
            return CAN_PROTOCOL_TIC12400_RESULT_CRC_TIMEOUT;
        case TIC12400_RESULT_CRC_COMPLETION_MISSING:
            return CAN_PROTOCOL_TIC12400_RESULT_CRC_COMPLETION_MISSING;
        default:
            return CAN_PROTOCOL_TIC12400_RESULT_UNKNOWN;
    }
}

static void TIC12400_CAN_RecordResult(
    CAN_Transport_Result_t result,
    volatile uint32_t *accepted_counter)
{
    g_tic12400_can.last_tx_result = result;

    if ((result == CAN_TRANSPORT_OK) ||
        (result == CAN_TRANSPORT_QUEUED))
    {
        (*accepted_counter)++;
    }
    else
    {
        g_tic12400_can.tx_failures++;
    }
}

static void TIC12400_CAN_SendStatus(void)
{
    CAN_Protocol_Tic12400Status_t status = {0};
    CAN_Transport_Result_t result;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    status.online = g_tic12400_probe.online;
    status.configuration_valid =
        g_tic12400_probe.configuration_passed;
    status.crc_complete = g_tic12400_probe.crc_completed;
    status.monitoring = g_tic12400_probe.monitoring_started;
    status.por_observed = g_tic12400_probe.por_observed;
    status.device_id = g_tic12400_probe.device_id;

    if (g_tic12400_probe.monitoring_started != 0U)
    {
        status.service_result = TIC12400_CAN_MapResult(
            g_tic12400_probe.service_result);
    }
    else
    {
        status.service_result = TIC12400_CAN_MapResult(
            g_tic12400_probe.result);
    }
    status.service_fault =
        (status.service_result != CAN_PROTOCOL_TIC12400_RESULT_OK) ?
        1U : 0U;

    status.spi_fail = g_tic12400_probe.status.spi_fail;
    status.parity_fail = g_tic12400_probe.status.parity_fail;
    status.switch_state_change =
        g_tic12400_probe.status.switch_state_change;
    status.supply_threshold =
        g_tic12400_probe.status.supply_threshold;
    status.temperature = g_tic12400_probe.status.temperature;
    status.other_interrupt =
        g_tic12400_probe.status.other_interrupt;
    status.power_on_reset =
        g_tic12400_probe.status.power_on_reset;
    status.service_failures = TIC12400_CAN_SaturateU16(
        g_tic12400_probe.service_failures);
    status.last_nonzero_int_status =
        (uint16_t)g_tic12400_probe.last_nonzero_int_status;

    CAN_Protocol_EncodeTic12400Status(&status, payload);
    result = CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_TIC12400_STATUS_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);
    TIC12400_CAN_RecordResult(
        result,
        &g_tic12400_can.status_frames_accepted);
}

static void TIC12400_CAN_SendSwitchState(void)
{
    CAN_Protocol_Tic12400SwitchState_t state = {0};
    TIC12400_ProbeSwitchState_t probe_state = {0};
    CAN_Transport_Result_t result;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    (void)TIC12400_Probe_GetSwitchState(&probe_state);
    state.closed_bitmap = probe_state.closed_bitmap;
    state.valid_mask = probe_state.valid_mask;
    state.generation = probe_state.generation;
    state.data_valid = probe_state.data_valid;

    CAN_Protocol_EncodeTic12400SwitchState(&state, payload);
    result = CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_TIC12400_SWITCH_STATE_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);
    TIC12400_CAN_RecordResult(
        result,
        &g_tic12400_can.switch_frames_accepted);
    g_tic12400_can.last_switch_generation =
        state.generation;
}

static void TIC12400_CAN_SendProfile(void)
{
    CAN_Protocol_Tic12400Profile_t profile = {0};
    CAN_Transport_Result_t result;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    profile.battery_switch_mask =
        g_tic12400_probe.battery_switch_mask;
    profile.battery_capable_mask =
        g_tic12400_probe.battery_capable_mask;
    profile.generation = g_tic12400_probe.profile_generation;
    profile.configuration_valid =
        g_tic12400_probe.configuration_passed;

    CAN_Protocol_EncodeTic12400Profile(&profile, payload);
    result = CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_TIC12400_PROFILE_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);
    TIC12400_CAN_RecordResult(
        result,
        &g_tic12400_can.profile_frames_accepted);
}

void TIC12400_CAN_Init(void)
{
    g_tic12400_can = (TIC12400_CanSnapshot_t){0};
    tic12400_can_last_status_tick = HAL_GetTick();
    tic12400_can_last_switch_tick = HAL_GetTick();
    tic12400_can_last_profile_tick = HAL_GetTick();
}

void TIC12400_CAN_Process(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - tic12400_can_last_status_tick) >=
        TIC12400_CAN_STATUS_PERIOD_MS)
    {
        tic12400_can_last_status_tick = now;
        TIC12400_CAN_SendStatus();
    }

    if ((now - tic12400_can_last_profile_tick) >=
        TIC12400_CAN_PROFILE_PERIOD_MS)
    {
        tic12400_can_last_profile_tick = now;
        TIC12400_CAN_SendProfile();
    }

    if ((g_tic12400_probe.switch_state_valid == 0U) ||
        (((now - tic12400_can_last_switch_tick) <
          TIC12400_CAN_SWITCH_PERIOD_MS) &&
         (g_tic12400_probe.switch_state_generation ==
          g_tic12400_can.last_switch_generation)))
    {
        return;
    }

    tic12400_can_last_switch_tick = now;
    TIC12400_CAN_SendSwitchState();
}
