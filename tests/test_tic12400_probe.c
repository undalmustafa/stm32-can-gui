#include "unity.h"

#include "main.h"
#include "tic12400_probe.h"
#include "tic12400_recovery.h"

#include <string.h>

#define TEST_REGISTER_COUNT       64U
#define TEST_CONFIG_TRIGGER       (1UL << 11U)
#define TEST_INT_STATUS_SSC       (1UL << 3U)
#define TEST_EXPECTED_CONFIG_WRITES 12U

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin);

SPI_HandleTypeDef test_spi;
GPIO_TypeDef test_gpio_a;
GPIO_TypeDef test_gpio_g;

static uint32_t fake_tick;
static uint32_t register_values[TEST_REGISTER_COUNT];
static uint32_t comparator_read_value;
static TIC12400_Result_t hardware_reset_result;
static uint32_t driver_init_calls;
static uint32_t hardware_reset_calls;
static uint32_t device_id_calls;
static uint32_t read_register_calls;
static uint32_t write_register_calls;
static uint32_t read_crc_calls;
static uint32_t read_calls_by_address[TEST_REGISTER_COUNT];
static uint32_t write_calls_by_address[TEST_REGISTER_COUNT];
static uint32_t delay_calls;
static uint32_t delayed_ms;
static uint32_t nvic_disable_calls;
static uint32_t nvic_enable_calls;
static IRQn_Type last_disabled_irq;
static IRQn_Type last_enabled_irq;

static uint32_t fail_device_id_call;
static TIC12400_Result_t fail_device_id_result;
static int32_t fail_read_address;
static uint8_t fail_read_persistent;
static TIC12400_Result_t fail_read_result;
static uint32_t fail_read_data;
static uint32_t fail_read_hal_error;
static int32_t mismatch_read_address;
static uint8_t mismatch_read_once;
static int32_t fail_write_address;
static TIC12400_Result_t fail_write_result;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

void HAL_Delay(uint32_t delay)
{
    delay_calls++;
    delayed_ms += delay;
    fake_tick += delay;
}

void HAL_NVIC_DisableIRQ(IRQn_Type IRQn)
{
    nvic_disable_calls++;
    last_disabled_irq = IRQn;
}

void HAL_NVIC_EnableIRQ(IRQn_Type IRQn)
{
    nvic_enable_calls++;
    last_enabled_irq = IRQn;
}

static TIC12400_Transaction_t transaction_with(
    TIC12400_Result_t result,
    uint32_t data)
{
    TIC12400_Transaction_t transaction = {0};

    transaction.result = result;
    transaction.hal_status =
        (result == TIC12400_RESULT_OK) ? HAL_OK : HAL_ERROR;
    transaction.hal_error =
        (result == TIC12400_RESULT_OK) ? 0U : fail_read_hal_error;
    transaction.tx_frame = 0x10000000UL | read_register_calls;
    transaction.rx_frame = 0x20000001UL | read_register_calls;
    transaction.data = data;
    return transaction;
}

void TIC12400_Driver_Init(TIC12400_Device_t *device,
                          SPI_HandleTypeDef *spi,
                          GPIO_TypeDef *chip_select_port,
                          uint16_t chip_select_pin,
                          GPIO_TypeDef *reset_port,
                          uint16_t reset_pin)
{
    driver_init_calls++;
    TEST_ASSERT_EQUAL_PTR(&test_spi, spi);
    TEST_ASSERT_EQUAL_PTR(TIC12400_CS_GPIO_Port, chip_select_port);
    TEST_ASSERT_EQUAL_UINT16(TIC12400_CS_Pin, chip_select_pin);
    TEST_ASSERT_EQUAL_PTR(TIC12400_RESET_GPIO_Port, reset_port);
    TEST_ASSERT_EQUAL_UINT16(TIC12400_RESET_Pin, reset_pin);
    device->spi = spi;
    device->chip_select_port = chip_select_port;
    device->chip_select_pin = chip_select_pin;
    device->reset_port = reset_port;
    device->reset_pin = reset_pin;
}

TIC12400_Result_t TIC12400_HardwareReset(
    const TIC12400_Device_t *device)
{
    hardware_reset_calls++;
    TEST_ASSERT_EQUAL_PTR(&test_spi, device->spi);
    (void)memset(register_values, 0, sizeof(register_values));
    return hardware_reset_result;
}

TIC12400_Transaction_t TIC12400_ReadDeviceId(
    const TIC12400_Device_t *device)
{
    TIC12400_Transaction_t transaction;

    TEST_ASSERT_EQUAL_PTR(&test_spi, device->spi);
    device_id_calls++;
    transaction = transaction_with(TIC12400_RESULT_OK,
                                   TIC12400_EXPECTED_DEVICE_ID);
    transaction.status.power_on_reset = 1U;
    if ((fail_device_id_call != 0U) &&
        (device_id_calls == fail_device_id_call))
    {
        transaction.result = fail_device_id_result;
        transaction.hal_status = HAL_ERROR;
        fail_device_id_call = 0U;
    }
    return transaction;
}

TIC12400_Transaction_t TIC12400_ReadRegister(
    const TIC12400_Device_t *device,
    uint8_t register_address)
{
    TIC12400_Transaction_t transaction;
    uint32_t data;

    TEST_ASSERT_EQUAL_PTR(&test_spi, device->spi);
    read_register_calls++;
    read_calls_by_address[register_address]++;

    if ((int32_t)register_address == fail_read_address)
    {
        transaction = transaction_with(fail_read_result, fail_read_data);
        if (fail_read_persistent == 0U)
        {
            fail_read_address = -1;
        }
        return transaction;
    }

    if (register_address == TIC12400_REGISTER_CONFIG)
    {
        if ((register_values[register_address] &
            TIC12400_CONFIG_CRC_TRIGGER_MASK) != 0U)
        {
            register_values[register_address] &=
                ~(uint32_t)TIC12400_CONFIG_CRC_TRIGGER_MASK;
            register_values[TIC12400_REGISTER_INT_STAT] |=
                TIC12400_INT_STATUS_CRC_CALC_MASK;
        }
    }

    if (register_address == TIC12400_REGISTER_IN_STAT_COMP)
    {
        data = comparator_read_value;
    }
    else
    {
        data = register_values[register_address];
    }

    if ((int32_t)register_address == mismatch_read_address)
    {
        data ^= 1UL;
        if (mismatch_read_once != 0U)
        {
            mismatch_read_address = -1;
        }
    }

    transaction = transaction_with(TIC12400_RESULT_OK, data);
    if (register_address == TIC12400_REGISTER_INT_STAT)
    {
        register_values[register_address] = 0U;
    }
    return transaction;
}

TIC12400_Transaction_t TIC12400_WriteRegister(
    const TIC12400_Device_t *device,
    uint8_t register_address,
    uint32_t register_data)
{
    TIC12400_Transaction_t transaction;

    TEST_ASSERT_EQUAL_PTR(&test_spi, device->spi);
    write_register_calls++;
    write_calls_by_address[register_address]++;
    if ((int32_t)register_address == fail_write_address)
    {
        return transaction_with(fail_write_result,
                                register_values[register_address]);
    }

    register_values[register_address] = register_data;
    if ((register_address == TIC12400_REGISTER_CONFIG) &&
        ((register_data & TEST_CONFIG_TRIGGER) != 0U))
    {
        register_values[TIC12400_REGISTER_INT_STAT] |=
            TEST_INT_STATUS_SSC;
    }
    transaction = transaction_with(TIC12400_RESULT_OK, register_data);
    return transaction;
}

TIC12400_Transaction_t TIC12400_ReadConfigurationCrc(
    const TIC12400_Device_t *device)
{
    TEST_ASSERT_EQUAL_PTR(&test_spi, device->spi);
    read_crc_calls++;
    return transaction_with(TIC12400_RESULT_OK, 0xA55AU);
}

uint8_t TIC12400_FrameHasOddParity(uint32_t frame)
{
    (void)frame;
    return 1U;
}

static void reset_activity(void)
{
    driver_init_calls = 0U;
    hardware_reset_calls = 0U;
    device_id_calls = 0U;
    read_register_calls = 0U;
    write_register_calls = 0U;
    read_crc_calls = 0U;
    delay_calls = 0U;
    delayed_ms = 0U;
    nvic_disable_calls = 0U;
    nvic_enable_calls = 0U;
    last_disabled_irq = (IRQn_Type)0;
    last_enabled_irq = (IRQn_Type)0;
    (void)memset(read_calls_by_address,
                 0,
                 sizeof(read_calls_by_address));
    (void)memset(write_calls_by_address,
                 0,
                 sizeof(write_calls_by_address));
}

static void reset_fixture(void)
{
    fake_tick = 0U;
    test_spi = (SPI_HandleTypeDef){0};
    test_gpio_a = (GPIO_TypeDef){0};
    test_gpio_g = (GPIO_TypeDef){0};
    comparator_read_value = 0U;
    hardware_reset_result = TIC12400_RESULT_OK;
    fail_device_id_call = 0U;
    fail_device_id_result = TIC12400_RESULT_HAL_ERROR;
    fail_read_address = -1;
    fail_read_persistent = 0U;
    fail_read_result = TIC12400_RESULT_HAL_ERROR;
    fail_read_data = 0U;
    fail_read_hal_error = 0x1234U;
    mismatch_read_address = -1;
    mismatch_read_once = 0U;
    fail_write_address = -1;
    fail_write_result = TIC12400_RESULT_HAL_ERROR;
    (void)memset(register_values, 0, sizeof(register_values));

    TIC12400_Probe_Init(NULL, 0U);
    reset_activity();
}

void setUp(void)
{
    reset_fixture();
}

void tearDown(void)
{
}

static void initialize_successfully(void)
{
    TIC12400_Probe_Init(&test_spi, 1U);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.monitoring_started);
}

void test_null_spi_never_reaches_driver_or_runtime_reinitialization(void)
{
    TIC12400_Probe_Init(NULL, 1U);

    TEST_ASSERT_EQUAL_UINT32(0U, driver_init_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, hardware_reset_calls);
    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.reinitialization_pending);

    fake_tick = TIC12400_RECOVERY_INITIAL_DELAY_MS;
    TIC12400_Probe_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, hardware_reset_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, g_tic12400_probe.reinitialization_attempts);
}

void test_successful_init_validates_configures_crc_and_baseline(void)
{
    TIC12400_ProbeSwitchState_t state;

    initialize_successfully();

    TEST_ASSERT_EQUAL_UINT32(1U, driver_init_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, hardware_reset_calls);
    TEST_ASSERT_EQUAL_UINT32(1001U, device_id_calls);
    TEST_ASSERT_EQUAL_UINT32(1000U,
                             g_tic12400_probe.validation_completed);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.validation_passed);
    TEST_ASSERT_EQUAL_UINT32(TEST_EXPECTED_CONFIG_WRITES,
                             g_tic12400_probe.configuration_write_count);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.crc_trigger_self_cleared);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.crc_completed);
    TEST_ASSERT_EQUAL_HEX32(0xA55AU, g_tic12400_probe.crc_value);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.baseline_established);
    TEST_ASSERT_EQUAL_UINT32(1U, g_tic12400_probe.comparator_sample_batches);
    TEST_ASSERT_EQUAL_UINT32(2U, delayed_ms);
    TEST_ASSERT_EQUAL_UINT8(1U, TIC12400_Probe_GetSwitchState(&state));
    TEST_ASSERT_EQUAL_HEX32(TIC12400_PROFILE_CARRIER_FITTED_MASK,
                            state.valid_mask);
    TEST_ASSERT_EQUAL_HEX32(TIC12400_PROFILE_CARRIER_FITTED_MASK,
                            state.closed_bitmap);
}

void test_validation_failure_schedules_retry_then_recovers(void)
{
    fail_device_id_call = 5U;
    TIC12400_Probe_Init(&test_spi, 1U);

    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT32(4U,
        g_tic12400_probe.validation_first_failure_index);
    TEST_ASSERT_EQUAL(TIC12400_RESULT_HAL_ERROR,
                      g_tic12400_probe.validation_first_failure_result);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.reinitialization_pending);
    TEST_ASSERT_EQUAL_UINT32(TIC12400_RECOVERY_INITIAL_DELAY_MS,
                             g_tic12400_probe.reinitialization_delay_ms);

    fake_tick = TIC12400_RECOVERY_INITIAL_DELAY_MS - 1U;
    TIC12400_Probe_Process();
    TEST_ASSERT_EQUAL_UINT32(1U, g_tic12400_probe.attempts);

    fake_tick = TIC12400_RECOVERY_INITIAL_DELAY_MS;
    TIC12400_Probe_Process();
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT32(2U, g_tic12400_probe.attempts);
    TEST_ASSERT_EQUAL_UINT32(1U,
                             g_tic12400_probe.reinitialization_attempts);
    TEST_ASSERT_EQUAL_UINT32(1U,
                             g_tic12400_probe.reinitialization_successes);
}

void test_configuration_readback_mismatch_fails_closed(void)
{
    mismatch_read_address = TIC12400_REGISTER_MODE;
    mismatch_read_once = 1U;
    TIC12400_Probe_Init(&test_spi, 1U);

    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.monitoring_started);
    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.configuration_passed);
    TEST_ASSERT_EQUAL_UINT32(TIC12400_REGISTER_MODE,
                             g_tic12400_probe.configuration_failure_address);
    TEST_ASSERT_EQUAL(TIC12400_RESULT_REGISTER_VERIFY_MISMATCH,
                      g_tic12400_probe.configuration_result);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.reinitialization_pending);
}

void test_fallback_poll_and_interrupt_service_update_switch_state(void)
{
    TIC12400_ProbeSwitchState_t state;

    initialize_successfully();
    reset_activity();

    fake_tick = 501U;
    TIC12400_Probe_Process();
    TEST_ASSERT_EQUAL_UINT32(0U, read_register_calls);

    fake_tick = 502U;
    TIC12400_Probe_Process();
    TEST_ASSERT_EQUAL_UINT32(1U,
        read_calls_by_address[TIC12400_REGISTER_INT_STAT]);
    TEST_ASSERT_EQUAL_UINT32(0U,
        read_calls_by_address[TIC12400_REGISTER_IN_STAT_COMP]);

    comparator_read_value = 1UL;
    register_values[TIC12400_REGISTER_INT_STAT] = TEST_INT_STATUS_SSC;
    fake_tick = 503U;
    TIC12400_Probe_NotifyInterruptFromIsr();
    TIC12400_Probe_Process();

    TEST_ASSERT_EQUAL_UINT32(1U, g_tic12400_probe.interrupt_count);
    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.interrupt_pending);
    TEST_ASSERT_EQUAL_UINT32(1U, g_tic12400_probe.switch_change_events);
    TEST_ASSERT_EQUAL_UINT8(1U, TIC12400_Probe_GetSwitchState(&state));
    TEST_ASSERT_BITS(1UL, 0U, state.closed_bitmap);
    TEST_ASSERT_BITS(1UL, 1UL, state.last_change_mask);
}

void test_failed_status_data_is_ignored_and_threshold_recovers(void)
{
    uint32_t baseline_ssc_events;
    uint8_t failure_index;

    initialize_successfully();
    baseline_ssc_events = g_tic12400_probe.ssc_events;
    reset_activity();
    fail_read_address = TIC12400_REGISTER_INT_STAT;
    fail_read_persistent = 1U;
    fail_read_data = TEST_INT_STATUS_SSC;

    for (failure_index = 0U; failure_index < 3U; failure_index++)
    {
        fake_tick = 10U + failure_index;
        TIC12400_Probe_NotifyInterruptFromIsr();
        TIC12400_Probe_Process();
    }

    TEST_ASSERT_EQUAL_UINT32(baseline_ssc_events,
                             g_tic12400_probe.ssc_events);
    TEST_ASSERT_EQUAL_UINT32(3U, g_tic12400_probe.service_failures);
    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT8(0U, g_tic12400_probe.monitoring_started);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.reinitialization_pending);

    fail_read_address = -1;
    fake_tick = 12U + TIC12400_RECOVERY_INITIAL_DELAY_MS;
    TIC12400_Probe_Process();
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.online);
    TEST_ASSERT_EQUAL_UINT32(3U, g_tic12400_probe.service_failures);
    TEST_ASSERT_EQUAL_UINT32(1U,
                             g_tic12400_probe.reinitialization_successes);
}

void test_battery_profile_reconfiguration_is_validated_and_idempotent(void)
{
    uint32_t writes_after_reconfiguration;

    initialize_successfully();
    reset_activity();

    TEST_ASSERT_EQUAL_UINT8(0U,
        TIC12400_Probe_SetBatterySwitchMask(1UL << 12U));
    TEST_ASSERT_EQUAL_UINT32(0U, write_register_calls);

    TEST_ASSERT_EQUAL_UINT8(1U,
        TIC12400_Probe_SetBatterySwitchMask(1UL));
    TEST_ASSERT_EQUAL_UINT32(TEST_EXPECTED_CONFIG_WRITES,
                             write_register_calls);
    TEST_ASSERT_EQUAL_HEX32(1UL,
        register_values[TIC12400_REGISTER_CS_SELECT]);
    TEST_ASSERT_EQUAL_HEX32(1UL,
                            g_tic12400_probe.battery_switch_mask);
    TEST_ASSERT_EQUAL_UINT8(2U, g_tic12400_probe.profile_generation);
    TEST_ASSERT_EQUAL_UINT32(1U, nvic_disable_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, nvic_enable_calls);
    TEST_ASSERT_EQUAL(TIC12400_INT_EXTI_IRQn, last_disabled_irq);
    TEST_ASSERT_EQUAL(TIC12400_INT_EXTI_IRQn, last_enabled_irq);

    writes_after_reconfiguration = write_register_calls;
    TEST_ASSERT_EQUAL_UINT8(1U,
        TIC12400_Probe_SetBatterySwitchMask(1UL));
    TEST_ASSERT_EQUAL_UINT32(writes_after_reconfiguration,
                             write_register_calls);
}

void test_exti_callback_filters_pin_and_switch_query_rejects_null(void)
{
    initialize_successfully();
    reset_activity();

    HAL_GPIO_EXTI_Callback((uint16_t)(TIC12400_INT_Pin << 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, g_tic12400_probe.interrupt_count);
    HAL_GPIO_EXTI_Callback(TIC12400_INT_Pin);
    TEST_ASSERT_EQUAL_UINT32(1U, g_tic12400_probe.interrupt_count);
    TEST_ASSERT_EQUAL_UINT8(1U, g_tic12400_probe.interrupt_pending);
    TEST_ASSERT_EQUAL_UINT8(0U, TIC12400_Probe_GetSwitchState(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(
        test_null_spi_never_reaches_driver_or_runtime_reinitialization);
    RUN_TEST(test_successful_init_validates_configures_crc_and_baseline);
    RUN_TEST(test_validation_failure_schedules_retry_then_recovers);
    RUN_TEST(test_configuration_readback_mismatch_fails_closed);
    RUN_TEST(test_fallback_poll_and_interrupt_service_update_switch_state);
    RUN_TEST(test_failed_status_data_is_ignored_and_threshold_recovers);
    RUN_TEST(
        test_battery_profile_reconfiguration_is_validated_and_idempotent);
    RUN_TEST(test_exti_callback_filters_pin_and_switch_query_rejects_null);
    return UNITY_END();
}
