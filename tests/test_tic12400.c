#include "unity.h"

#include "tic12400.h"

#include <string.h>

static SPI_HandleTypeDef fake_spi;
static GPIO_TypeDef fake_cs_port;
static GPIO_TypeDef fake_reset_port;
static TIC12400_Device_t device;
static HAL_StatusTypeDef fake_hal_status;
static uint32_t fake_hal_error;
static uint8_t fake_rx[4];
static uint8_t captured_tx[4];
static uint16_t captured_size;
static uint32_t captured_timeout;
static GPIO_PinState cs_states[4];
static uint8_t cs_state_count;
static GPIO_PinState reset_states[4];
static uint8_t reset_state_count;
static uint32_t delay_values[4];
static uint8_t delay_count;

void HAL_Delay(uint32_t delay)
{
    if (delay_count < 4U)
    {
        delay_values[delay_count] = delay;
        delay_count++;
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port,
                       uint16_t pin,
                       GPIO_PinState state)
{
    (void)pin;

    if ((port == &fake_cs_port) && (cs_state_count < 4U))
    {
        cs_states[cs_state_count] = state;
        cs_state_count++;
    }
    else if ((port == &fake_reset_port) &&
             (reset_state_count < 4U))
    {
        reset_states[reset_state_count] = state;
        reset_state_count++;
    }
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(
    SPI_HandleTypeDef *spi,
    const uint8_t *tx,
    uint8_t *rx,
    uint16_t size,
    uint32_t timeout)
{
    (void)spi;
    captured_size = size;
    captured_timeout = timeout;
    memcpy(captured_tx, tx, size);
    memcpy(rx, fake_rx, size);
    return fake_hal_status;
}

uint32_t HAL_SPI_GetError(const SPI_HandleTypeDef *spi)
{
    (void)spi;
    return fake_hal_error;
}

void setUp(void)
{
    memset(&fake_spi, 0, sizeof(fake_spi));
    memset(fake_rx, 0, sizeof(fake_rx));
    memset(captured_tx, 0, sizeof(captured_tx));
    memset(cs_states, 0, sizeof(cs_states));
    memset(reset_states, 0, sizeof(reset_states));
    memset(delay_values, 0, sizeof(delay_values));
    fake_hal_status = HAL_OK;
    fake_hal_error = 0U;
    captured_size = 0U;
    captured_timeout = 0U;
    cs_state_count = 0U;
    reset_state_count = 0U;
    delay_count = 0U;

    TIC12400_Driver_Init(&device,
                         &fake_spi,
                         &fake_cs_port,
                         0x0010U,
                         &fake_reset_port,
                         0x0004U);
}

void tearDown(void)
{
}

void test_build_device_id_read_frame_uses_odd_parity(void)
{
    uint32_t frame =
        TIC12400_BuildReadFrame(TIC12400_REGISTER_DEVICE_ID);

    TEST_ASSERT_EQUAL_UINT32(0x02000000UL, frame);
    TEST_ASSERT_EQUAL_UINT8(1U, TIC12400_FrameHasOddParity(frame));
}

void test_build_read_frame_sets_parity_bit_when_needed(void)
{
    uint32_t frame =
        TIC12400_BuildReadFrame(TIC12400_REGISTER_CRC);

    TEST_ASSERT_EQUAL_UINT32(0x06000001UL, frame);
    TEST_ASSERT_EQUAL_UINT8(1U, TIC12400_FrameHasOddParity(frame));
}

void test_build_configuration_crc_trigger_frame(void)
{
    uint32_t frame = TIC12400_BuildWriteFrame(
        TIC12400_REGISTER_CONFIG,
        TIC12400_CONFIG_CRC_TRIGGER_MASK);

    TEST_ASSERT_EQUAL_UINT32(0xB4000400UL, frame);
    TEST_ASSERT_EQUAL_UINT8(1U, TIC12400_FrameHasOddParity(frame));
}

void test_build_write_frame_encodes_address_data_and_odd_parity(void)
{
    uint32_t frame = TIC12400_BuildWriteFrame(
        TIC12400_REGISTER_IN_EN,
        0x000001U);

    TEST_ASSERT_EQUAL_UINT32(0xB6000003UL, frame);
    TEST_ASSERT_EQUAL_UINT8(1U, TIC12400_FrameHasOddParity(frame));
}

void test_read_device_id_decodes_data_and_por_status(void)
{
    TIC12400_Transaction_t transaction;

    fake_rx[0] = 0x80U;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x00U;
    fake_rx[3] = 0x41U;

    transaction = TIC12400_ReadDeviceId(&device);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_OK, transaction.result);
    TEST_ASSERT_EQUAL_UINT32(TIC12400_EXPECTED_DEVICE_ID,
                             transaction.data);
    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.power_on_reset);
    TEST_ASSERT_EQUAL_UINT8(4U, captured_size);
    TEST_ASSERT_EQUAL_UINT32(10U, captured_timeout);
    TEST_ASSERT_EQUAL_UINT8(0x02U, captured_tx[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[3]);
    TEST_ASSERT_EQUAL_UINT8(2U, cs_state_count);
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, cs_states[0]);
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, cs_states[1]);
}

void test_read_device_id_rejects_wrong_id(void)
{
    TIC12400_Transaction_t transaction;

    fake_rx[0] = 0x02U;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x00U;
    fake_rx[3] = 0x42U;

    transaction = TIC12400_ReadDeviceId(&device);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_DEVICE_ID_MISMATCH,
                      transaction.result);
    TEST_ASSERT_EQUAL_UINT32(0x21U, transaction.data);
}

void test_read_register_rejects_bad_response_parity(void)
{
    TIC12400_Transaction_t transaction;

    fake_rx[0] = 0x02U;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x00U;
    fake_rx[3] = 0x40U;

    transaction = TIC12400_ReadRegister(
        &device,
        TIC12400_REGISTER_DEVICE_ID);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_RESPONSE_PARITY_ERROR,
                      transaction.result);
}

void test_read_register_reports_device_parity_failure(void)
{
    TIC12400_Transaction_t transaction;

    fake_rx[0] = 0x20U;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x00U;
    fake_rx[3] = 0x41U;

    transaction = TIC12400_ReadRegister(
        &device,
        TIC12400_REGISTER_DEVICE_ID);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_DEVICE_PARITY_ERROR,
                      transaction.result);
    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.parity_fail);
}

void test_read_register_reports_latched_spi_failure_and_preserves_data(void)
{
    TIC12400_Transaction_t transaction;

    /*
     * SPI_FAIL is a latched status flag while the current DEVICE_ID
     * payload and response parity are valid.
     */
    fake_rx[0] = 0x40U;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x00U;
    fake_rx[3] = 0x41U;

    transaction = TIC12400_ReadRegister(
        &device,
        TIC12400_REGISTER_DEVICE_ID);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_DEVICE_SPI_ERROR,
                      transaction.result);
    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.spi_fail);
    TEST_ASSERT_EQUAL_UINT32(TIC12400_EXPECTED_DEVICE_ID,
                             transaction.data);
    TEST_ASSERT_EQUAL_UINT8(1U,
                           TIC12400_FrameHasOddParity(
                               transaction.rx_frame));
}

void test_read_register_decodes_all_status_bit_positions(void)
{
    TIC12400_Transaction_t transaction;

    /* Bits 31 through 25 are POR, SPI, parity, SSC, VS, TEMP, and OI. */
    fake_rx[0] = 0xFEU;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x00U;
    fake_rx[3] = 0x00U;

    transaction = TIC12400_ReadRegister(
        &device,
        TIC12400_REGISTER_INT_STAT);

    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.power_on_reset);
    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.spi_fail);
    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.parity_fail);
    TEST_ASSERT_EQUAL_UINT8(1U,
                           transaction.status.switch_state_change);
    TEST_ASSERT_EQUAL_UINT8(1U,
                           transaction.status.supply_threshold);
    TEST_ASSERT_EQUAL_UINT8(1U, transaction.status.temperature);
    TEST_ASSERT_EQUAL_UINT8(1U,
                           transaction.status.other_interrupt);
}

void test_hal_failure_releases_chip_select_and_keeps_error(void)
{
    TIC12400_Transaction_t transaction;

    fake_hal_status = HAL_TIMEOUT;
    fake_hal_error = 0x1234U;

    transaction = TIC12400_ReadRegister(
        &device,
        TIC12400_REGISTER_DEVICE_ID);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_HAL_ERROR, transaction.result);
    TEST_ASSERT_EQUAL(HAL_TIMEOUT, transaction.hal_status);
    TEST_ASSERT_EQUAL_UINT32(0x1234U, transaction.hal_error);
    TEST_ASSERT_EQUAL_UINT8(2U, cs_state_count);
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, cs_states[0]);
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, cs_states[1]);
}

void test_write_register_transmits_frame_and_decodes_previous_value(void)
{
    TIC12400_Transaction_t transaction;

    /*
     * The write response contains the register's previous value.
     * 0x00000001 represents previous value zero plus odd parity.
     */
    fake_rx[3] = 0x01U;

    transaction = TIC12400_WriteRegister(
        &device,
        TIC12400_REGISTER_IN_EN,
        0x000001U);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_OK, transaction.result);
    TEST_ASSERT_EQUAL_UINT32(0U, transaction.data);
    TEST_ASSERT_EQUAL_UINT8(0xB6U, captured_tx[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x03U, captured_tx[3]);
    TEST_ASSERT_EQUAL_UINT8(2U, cs_state_count);
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, cs_states[0]);
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, cs_states[1]);
}

void test_write_register_rejects_data_wider_than_24_bits(void)
{
    TIC12400_Transaction_t transaction = TIC12400_WriteRegister(
        &device,
        TIC12400_REGISTER_IN_EN,
        0x01000000UL);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_INVALID_DATA,
                      transaction.result);
    TEST_ASSERT_EQUAL_UINT8(0U, captured_size);
    TEST_ASSERT_EQUAL_UINT8(0U, cs_state_count);
}

void test_read_configuration_crc_returns_16_bit_result(void)
{
    TIC12400_Transaction_t transaction;

    /* CRC value 0x1234 shifted into response data; parity is already odd. */
    fake_rx[0] = 0x00U;
    fake_rx[1] = 0x00U;
    fake_rx[2] = 0x24U;
    fake_rx[3] = 0x68U;

    transaction = TIC12400_ReadConfigurationCrc(&device);

    TEST_ASSERT_EQUAL(TIC12400_RESULT_OK, transaction.result);
    TEST_ASSERT_EQUAL_UINT32(0x1234U, transaction.data);
    TEST_ASSERT_EQUAL_UINT8(0x06U, captured_tx[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00U, captured_tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01U, captured_tx[3]);
}

void test_hardware_reset_pulses_active_high(void)
{
    TEST_ASSERT_EQUAL(TIC12400_RESULT_OK,
                      TIC12400_HardwareReset(&device));
    TEST_ASSERT_EQUAL_UINT8(2U, reset_state_count);
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, reset_states[0]);
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, reset_states[1]);
    TEST_ASSERT_EQUAL_UINT8(2U, delay_count);
    TEST_ASSERT_EQUAL_UINT32(2U, delay_values[0]);
    TEST_ASSERT_EQUAL_UINT32(2U, delay_values[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_device_id_read_frame_uses_odd_parity);
    RUN_TEST(test_build_read_frame_sets_parity_bit_when_needed);
    RUN_TEST(test_build_configuration_crc_trigger_frame);
    RUN_TEST(
        test_build_write_frame_encodes_address_data_and_odd_parity);
    RUN_TEST(test_read_device_id_decodes_data_and_por_status);
    RUN_TEST(test_read_device_id_rejects_wrong_id);
    RUN_TEST(test_read_register_rejects_bad_response_parity);
    RUN_TEST(test_read_register_reports_device_parity_failure);
    RUN_TEST(
        test_read_register_reports_latched_spi_failure_and_preserves_data);
    RUN_TEST(test_read_register_decodes_all_status_bit_positions);
    RUN_TEST(test_hal_failure_releases_chip_select_and_keeps_error);
    RUN_TEST(
        test_write_register_transmits_frame_and_decodes_previous_value);
    RUN_TEST(test_write_register_rejects_data_wider_than_24_bits);
    RUN_TEST(test_read_configuration_crc_returns_16_bit_result);
    RUN_TEST(test_hardware_reset_pulses_active_high);
    return UNITY_END();
}
