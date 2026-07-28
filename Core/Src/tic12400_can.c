#include "tic12400_can.h"

#include "can_protocol.h"
#include "tic12400_probe.h"

#define TIC12400_CAN_STATUS_PERIOD_MS 500U
#define TIC12400_CAN_ADC_FRAME_PERIOD_MS 40U

_Static_assert(
    CAN_PROTOCOL_TIC12400_ADC_CHANNEL_COUNT ==
    TIC12400_ADC_CHANNEL_COUNT,
    "TIC12400 protocol and driver channel counts must match");
_Static_assert(
    (CAN_PROTOCOL_TIC12400_ADC_GROUP_COUNT *
     CAN_PROTOCOL_TIC12400_ADC_CODES_PER_FRAME) ==
    CAN_PROTOCOL_TIC12400_ADC_CHANNEL_COUNT,
    "TIC12400 ADC groups must cover every channel");

volatile TIC12400_CanSnapshot_t g_tic12400_can;

static uint32_t tic12400_can_last_status_tick;
static uint32_t tic12400_can_last_adc_tick;
static uint16_t tic12400_can_adc_snapshot[
    CAN_PROTOCOL_TIC12400_ADC_CHANNEL_COUNT];

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
    status.adc_characterization =
        g_tic12400_probe.adc_characterization_active;
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

static void TIC12400_CAN_CaptureAdcSnapshot(void)
{
    uint8_t channel;

    g_tic12400_can.adc_generation++;

    for (channel = 0U;
         channel < CAN_PROTOCOL_TIC12400_ADC_CHANNEL_COUNT;
         channel++)
    {
        tic12400_can_adc_snapshot[channel] =
            g_tic12400_probe.adc_raw[channel];
    }
}

static void TIC12400_CAN_SendNextAdcGroup(void)
{
    CAN_Protocol_Tic12400AdcGroup_t group = {0};
    CAN_Transport_Result_t result;
    uint8_t code_index;
    uint8_t first_channel;
    uint8_t payload[CAN_PROTOCOL_PAYLOAD_SIZE] = {0};

    if (g_tic12400_can.next_adc_group == 0U)
    {
        TIC12400_CAN_CaptureAdcSnapshot();
    }

    group.generation = g_tic12400_can.adc_generation;
    group.group_index = g_tic12400_can.next_adc_group;
    first_channel = (uint8_t)(
        group.group_index *
        CAN_PROTOCOL_TIC12400_ADC_CODES_PER_FRAME);

    for (code_index = 0U;
         code_index < CAN_PROTOCOL_TIC12400_ADC_CODES_PER_FRAME;
         code_index++)
    {
        group.adc_code[code_index] =
            tic12400_can_adc_snapshot[first_channel + code_index];
    }

    CAN_Protocol_EncodeTic12400AdcGroup(&group, payload);
    result = CAN_Transport_SendClassicLatest(
        CAN_PROTOCOL_TIC12400_ADC_TX_ID,
        CAN_TRANSPORT_ID_STANDARD,
        payload);
    TIC12400_CAN_RecordResult(
        result,
        &g_tic12400_can.adc_frames_accepted);

    g_tic12400_can.next_adc_group++;
    if (g_tic12400_can.next_adc_group >=
        CAN_PROTOCOL_TIC12400_ADC_GROUP_COUNT)
    {
        g_tic12400_can.next_adc_group = 0U;
    }
}

void TIC12400_CAN_Init(void)
{
    g_tic12400_can = (TIC12400_CanSnapshot_t){0};
    tic12400_can_last_status_tick = HAL_GetTick();
    tic12400_can_last_adc_tick = HAL_GetTick();
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

    if ((g_tic12400_probe.adc_characterization_active == 0U) ||
        (g_tic12400_probe.adc_sample_batches == 0U) ||
        ((now - tic12400_can_last_adc_tick) <
         TIC12400_CAN_ADC_FRAME_PERIOD_MS))
    {
        return;
    }

    tic12400_can_last_adc_tick = now;
    TIC12400_CAN_SendNextAdcGroup();
}
