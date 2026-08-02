#include "unity.h"

#include "boot_flash.h"

#include <string.h>

typedef struct
{
    uint8_t record_0[BOOT_CONTROL_RECORD_SIZE];
    uint8_t record_1[BOOT_CONTROL_RECORD_SIZE];
    uint32_t program_addresses[4];
    uint8_t unlock_succeeds;
    uint8_t lock_succeeds;
    uint8_t erase_succeeds;
    uint8_t leave_erase_corruption;
    uint8_t fail_program_call;
    uint8_t partial_bytes_on_failure;
    uint8_t corrupt_read_at_program_count;
    uint8_t unlock_calls;
    uint8_t lock_calls;
    uint8_t erase_calls;
    uint8_t program_calls;
} FlashFixture_t;

static FlashFixture_t fixture;
static Boot_FlashBackend_t backend;
static Boot_ControlState_t old_state;
static Boot_ControlState_t new_state;
static Boot_FlashEvidence_t evidence;

static uint8_t *AddressToRecord(FlashFixture_t *flash_fixture,
                                uint32_t address,
                                uint32_t length)
{
    uint32_t offset;

    if ((address >= BOOT_CONTROL_RECORD_0_ADDRESS) &&
        (address <= (BOOT_CONTROL_RECORD_0_ADDRESS +
                     BOOT_CONTROL_RECORD_SIZE)) &&
        (length <= ((BOOT_CONTROL_RECORD_0_ADDRESS +
                     BOOT_CONTROL_RECORD_SIZE) - address)))
    {
        offset = (uint32_t)(address - BOOT_CONTROL_RECORD_0_ADDRESS);
        return &flash_fixture->record_0[offset];
    }
    if ((address >= BOOT_CONTROL_RECORD_1_ADDRESS) &&
        (address <= (BOOT_CONTROL_RECORD_1_ADDRESS +
                     BOOT_CONTROL_RECORD_SIZE)) &&
        (length <= ((BOOT_CONTROL_RECORD_1_ADDRESS +
                     BOOT_CONTROL_RECORD_SIZE) - address)))
    {
        offset = (uint32_t)(address - BOOT_CONTROL_RECORD_1_ADDRESS);
        return &flash_fixture->record_1[offset];
    }
    return NULL;
}

static uint8_t Unlock(void *context)
{
    FlashFixture_t *flash_fixture = (FlashFixture_t *)context;

    flash_fixture->unlock_calls++;
    return flash_fixture->unlock_succeeds;
}

static uint8_t Lock(void *context)
{
    FlashFixture_t *flash_fixture = (FlashFixture_t *)context;

    flash_fixture->lock_calls++;
    return flash_fixture->lock_succeeds;
}

static uint8_t EraseSector(void *context, uint32_t sector_address)
{
    FlashFixture_t *flash_fixture = (FlashFixture_t *)context;
    uint8_t *record = AddressToRecord(
        flash_fixture, sector_address, BOOT_CONTROL_RECORD_SIZE);

    flash_fixture->erase_calls++;
    TEST_ASSERT_NOT_NULL(record);
    if (flash_fixture->erase_succeeds != 0U)
    {
        (void)memset(record, 0xFF, BOOT_CONTROL_RECORD_SIZE);
        if (flash_fixture->leave_erase_corruption != 0U)
        {
            record[BOOT_CONTROL_RECORD_SIZE - 1U] = 0U;
        }
        return 1U;
    }

    (void)memset(record, 0xFF, BOOT_FLASH_PROGRAM_UNIT_SIZE);
    return 0U;
}

static uint8_t ProgramUnit(
    void *context,
    uint32_t address,
    const uint8_t data[BOOT_FLASH_PROGRAM_UNIT_SIZE])
{
    FlashFixture_t *flash_fixture = (FlashFixture_t *)context;
    uint8_t *destination = AddressToRecord(
        flash_fixture, address, BOOT_FLASH_PROGRAM_UNIT_SIZE);
    uint8_t byte_count = BOOT_FLASH_PROGRAM_UNIT_SIZE;
    uint8_t byte_index;

    TEST_ASSERT_NOT_NULL(destination);
    TEST_ASSERT_TRUE(flash_fixture->program_calls < 4U);
    flash_fixture->program_addresses[flash_fixture->program_calls] = address;
    flash_fixture->program_calls++;
    if (flash_fixture->program_calls == flash_fixture->fail_program_call)
    {
        byte_count = flash_fixture->partial_bytes_on_failure;
    }

    for (byte_index = 0U; byte_index < byte_count; byte_index++)
    {
        destination[byte_index] &= data[byte_index];
    }
    if (flash_fixture->program_calls == flash_fixture->fail_program_call)
    {
        return 0U;
    }
    return 1U;
}

static uint8_t Read(void *context,
                    uint32_t address,
                    uint8_t *data,
                    uint32_t length)
{
    FlashFixture_t *flash_fixture = (FlashFixture_t *)context;
    uint8_t *source = AddressToRecord(flash_fixture, address, length);

    if ((source == NULL) || (data == NULL))
    {
        return 0U;
    }
    (void)memcpy(data, source, length);
    if (flash_fixture->program_calls ==
        flash_fixture->corrupt_read_at_program_count)
    {
        data[0] ^= 1U;
    }
    return 1U;
}

static void PrepareCommitted(const Boot_ControlState_t *state,
                             uint8_t record[BOOT_CONTROL_RECORD_SIZE])
{
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_PrepareRecord(state, record));
    Boot_Control_MarkRecordCommitted(record);
}

static void ResetFixture(void)
{
    (void)memset(&fixture, 0, sizeof(fixture));
    (void)memset(fixture.record_0, 0xFF, sizeof(fixture.record_0));
    (void)memset(fixture.record_1, 0xFF, sizeof(fixture.record_1));
    fixture.unlock_succeeds = 1U;
    fixture.lock_succeeds = 1U;
    fixture.erase_succeeds = 1U;
    fixture.corrupt_read_at_program_count = UINT8_MAX;
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Initialize(&old_state, BOOT_SLOT_A, 5U, 100U));
    PrepareCommitted(&old_state, fixture.record_0);
    new_state = old_state;
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(
            &new_state, BOOT_SLOT_B, 6U, 200U));

    backend.unlock = Unlock;
    backend.lock = Lock;
    backend.erase_sector = EraseSector;
    backend.program_unit = ProgramUnit;
    backend.read = Read;
    backend.context = &fixture;
}

void setUp(void)
{
    ResetFixture();
}

void tearDown(void)
{
}

static void AssertOldRecordRemainsBootable(void)
{
    Boot_ControlState_t loaded;
    uint8_t selected;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Load(fixture.record_0,
                          fixture.record_1,
                          &loaded,
                          &selected));
    TEST_ASSERT_EQUAL_UINT8(0U, selected);
    TEST_ASSERT_EQUAL_UINT32(old_state.generation, loaded.generation);
}

static void test_persist_programs_commit_unit_last_and_selects_new_record(void)
{
    Boot_ControlState_t loaded;
    uint8_t selected;
    uint8_t program_index;

    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_OK,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(3U, evidence.body_units_programmed);
    TEST_ASSERT_EQUAL_UINT8(1U, evidence.commit_programmed);
    TEST_ASSERT_EQUAL_UINT8(1U, evidence.locked);
    TEST_ASSERT_EQUAL_UINT8(4U, fixture.program_calls);
    for (program_index = 0U; program_index < 4U; program_index++)
    {
        TEST_ASSERT_EQUAL_HEX32(
            BOOT_CONTROL_RECORD_1_ADDRESS +
                ((uint32_t)program_index * BOOT_FLASH_PROGRAM_UNIT_SIZE),
            fixture.program_addresses[program_index]);
    }
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Load(fixture.record_0,
                          fixture.record_1,
                          &loaded,
                          &selected));
    TEST_ASSERT_EQUAL_UINT8(1U, selected);
    TEST_ASSERT_EQUAL_UINT32(new_state.generation, loaded.generation);
}

static void test_power_loss_during_any_program_unit_keeps_old_record(void)
{
    uint8_t failed_call;

    for (failed_call = 1U; failed_call <= 4U; failed_call++)
    {
        ResetFixture();
        fixture.fail_program_call = failed_call;
        fixture.partial_bytes_on_failure =
            (failed_call == 4U) ? 15U : 8U;
        TEST_ASSERT_NOT_EQUAL(
            BOOT_FLASH_RESULT_OK,
            Boot_Flash_PersistControl(
                &backend, &new_state, 1U, &evidence));
        TEST_ASSERT_EQUAL_UINT8(1U, fixture.lock_calls);
        AssertOldRecordRemainsBootable();
    }
}

static void test_erase_and_readback_failures_never_reach_commit(void)
{
    fixture.erase_succeeds = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_ERASE_FAILED,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.program_calls);
    AssertOldRecordRemainsBootable();

    ResetFixture();
    fixture.leave_erase_corruption = 1U;
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_ERASE_VERIFY_FAILED,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.program_calls);
    AssertOldRecordRemainsBootable();

    ResetFixture();
    fixture.corrupt_read_at_program_count = 3U;
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_BODY_VERIFY_FAILED,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(3U, fixture.program_calls);
    AssertOldRecordRemainsBootable();
}

static void test_final_verify_and_lock_failures_are_reported(void)
{
    fixture.corrupt_read_at_program_count = 4U;
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_FINAL_VERIFY_FAILED,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(1U, evidence.commit_programmed);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.lock_calls);

    ResetFixture();
    fixture.lock_succeeds = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_LOCK_FAILED,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(0U, evidence.locked);
}

static void test_invalid_input_and_unlock_failure_have_no_flash_writes(void)
{
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_INVALID_ARGUMENT,
        Boot_Flash_PersistControl(
            &backend, &new_state, 2U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.erase_calls);

    fixture.unlock_succeeds = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_FLASH_RESULT_UNLOCK_FAILED,
        Boot_Flash_PersistControl(
            &backend, &new_state, 1U, &evidence));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.erase_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.lock_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_persist_programs_commit_unit_last_and_selects_new_record);
    RUN_TEST(test_power_loss_during_any_program_unit_keeps_old_record);
    RUN_TEST(test_erase_and_readback_failures_never_reach_commit);
    RUN_TEST(test_final_verify_and_lock_failures_are_reported);
    RUN_TEST(test_invalid_input_and_unlock_failure_have_no_flash_writes);
    return UNITY_END();
}
