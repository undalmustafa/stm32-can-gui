#include "unity.h"

#include "boot_control.h"
#include "boot_memory_map.h"

#include <string.h>

static Boot_ControlState_t state;
static uint8_t record_0[BOOT_CONTROL_RECORD_SIZE];
static uint8_t record_1[BOOT_CONTROL_RECORD_SIZE];

static void PrepareCommitted(const Boot_ControlState_t *source,
                             uint8_t record[BOOT_CONTROL_RECORD_SIZE])
{
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_PrepareRecord(source, record));
    Boot_Control_MarkRecordCommitted(record);
}

void setUp(void)
{
    (void)memset(record_0, 0xFF, sizeof(record_0));
    (void)memset(record_1, 0xFF, sizeof(record_1));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Initialize(&state, BOOT_SLOT_A, 5U, 100U));
}

void tearDown(void)
{
}

static void test_committed_record_round_trips_and_uncommitted_is_ignored(void)
{
    Boot_ControlState_t loaded;
    uint8_t selected;

    PrepareCommitted(&state, record_0);
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Load(record_0, record_1, &loaded, &selected));
    TEST_ASSERT_EQUAL_UINT8(0U, selected);
    TEST_ASSERT_EQUAL_UINT32(1U, loaded.generation);
    TEST_ASSERT_EQUAL(BOOT_SLOT_A, loaded.confirmed_slot);
    TEST_ASSERT_EQUAL_UINT32(5U, loaded.minimum_security_counter);

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_PrepareRecord(&state, record_1));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Load(record_0, record_1, &loaded, &selected));
    TEST_ASSERT_EQUAL_UINT8(0U, selected);
    TEST_ASSERT_EQUAL_UINT32(1U, loaded.generation);
}

static void test_crc_corruption_and_missing_records_fail_closed(void)
{
    Boot_ControlState_t loaded;
    uint8_t selected;

    PrepareCommitted(&state, record_0);
    record_0[20] ^= 0x01U;
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_NO_VALID_RECORD,
        Boot_Control_Load(record_0, record_1, &loaded, &selected));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, selected);

    (void)memset(record_0, 0xFF, sizeof(record_0));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_NO_VALID_RECORD,
        Boot_Control_Load(record_0, record_1, &loaded, &selected));
}

static void test_newest_generation_is_selected_across_counter_wrap(void)
{
    Boot_ControlState_t loaded;
    uint8_t selected;

    state.generation = UINT32_MAX;
    PrepareCommitted(&state, record_0);
    state.generation = 1U;
    PrepareCommitted(&state, record_1);

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Load(record_0, record_1, &loaded, &selected));
    TEST_ASSERT_EQUAL_UINT8(1U, selected);
    TEST_ASSERT_EQUAL_UINT32(1U, loaded.generation);
}

static void test_equal_generation_with_different_state_is_rejected(void)
{
    Boot_ControlState_t conflicting;
    Boot_ControlState_t loaded;
    uint8_t selected;

    PrepareCommitted(&state, record_0);
    conflicting = state;
    conflicting.confirmed_build_version = 101U;
    PrepareCommitted(&conflicting, record_1);
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_RECORD_CONFLICT,
        Boot_Control_Load(record_0, record_1, &loaded, &selected));
}

static void test_update_requires_other_slot_and_newer_security_counter(void)
{
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_INVALID_STATE,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_A, 6U, 200U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_SECURITY_ROLLBACK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 5U, 200U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    TEST_ASSERT_EQUAL(BOOT_SLOT_B, state.pending_slot);
    TEST_ASSERT_EQUAL_UINT8(
        BOOT_CONTROL_MAX_PENDING_ATTEMPTS,
        state.attempts_remaining);
    TEST_ASSERT_EQUAL_UINT32(2U, state.generation);

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_INVALID_STATE,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 7U, 300U));
}

static void test_pending_image_gets_three_persisted_attempts_then_rolls_back(void)
{
    Boot_Decision_t decision;
    uint8_t changed;
    uint8_t expected_attempts;
    uint8_t attempt;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));

    for (attempt = 0U;
         attempt < BOOT_CONTROL_MAX_PENDING_ATTEMPTS;
         attempt++)
    {
        expected_attempts = (uint8_t)(
            BOOT_CONTROL_MAX_PENDING_ATTEMPTS - attempt - 1U);
        TEST_ASSERT_EQUAL(
            BOOT_CONTROL_RESULT_OK,
            Boot_Control_SelectBoot(
                &state, 1U, 1U, &decision, &changed));
        TEST_ASSERT_EQUAL(BOOT_DECISION_SLOT_B, decision);
        TEST_ASSERT_EQUAL_UINT8(1U, changed);
        TEST_ASSERT_EQUAL_UINT8(expected_attempts,
                                state.attempts_remaining);
        TEST_ASSERT_EQUAL(
            BOOT_CONTROL_RESULT_OK,
            Boot_Control_PrepareRecord(&state, record_0));
    }

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_SelectBoot(&state, 1U, 1U, &decision, &changed));
    TEST_ASSERT_EQUAL(BOOT_DECISION_SLOT_A, decision);
    TEST_ASSERT_EQUAL_UINT8(1U, changed);
    TEST_ASSERT_EQUAL(BOOT_SLOT_NONE, state.pending_slot);
    TEST_ASSERT_EQUAL_UINT32(5U, state.minimum_security_counter);
}

static void test_invalid_pending_image_rolls_back_without_attempting_it(void)
{
    Boot_Decision_t decision;
    uint8_t changed;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_SelectBoot(&state, 1U, 0U, &decision, &changed));
    TEST_ASSERT_EQUAL(BOOT_DECISION_SLOT_A, decision);
    TEST_ASSERT_EQUAL_UINT8(1U, changed);
    TEST_ASSERT_EQUAL(BOOT_SLOT_NONE, state.pending_slot);
}

static void test_confirmation_promotes_pending_and_advances_anti_rollback(void)
{
    Boot_Decision_t decision;
    uint8_t changed;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_SelectBoot(&state, 1U, 1U, &decision, &changed));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ConfirmRunning(
            &state, BOOT_SLOT_B, 6U, 200U, &changed));
    TEST_ASSERT_EQUAL_UINT8(1U, changed);
    TEST_ASSERT_EQUAL(BOOT_SLOT_B, state.confirmed_slot);
    TEST_ASSERT_EQUAL(BOOT_SLOT_NONE, state.pending_slot);
    TEST_ASSERT_EQUAL_UINT32(6U, state.minimum_security_counter);

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ConfirmRunning(
            &state, BOOT_SLOT_B, 6U, 200U, &changed));
    TEST_ASSERT_EQUAL_UINT8(0U, changed);
}

static void test_wrong_image_cannot_confirm_and_invalid_confirmed_enters_recovery(void)
{
    Boot_Decision_t decision;
    uint8_t changed;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_INVALID_STATE,
        Boot_Control_ConfirmRunning(
            &state, BOOT_SLOT_B, 7U, 200U, &changed));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_SelectBoot(&state, 0U, 0U, &decision, &changed));
    TEST_ASSERT_EQUAL(BOOT_DECISION_RECOVERY, decision);
    TEST_ASSERT_EQUAL(BOOT_SLOT_NONE, state.pending_slot);
}

static void test_redundant_records_occupy_distinct_flash_sectors(void)
{
    TEST_ASSERT_EQUAL_HEX32(
        BOOT_CONTROL_RECORD_0_ADDRESS + BOOT_FLASH_SECTOR_SIZE,
        BOOT_CONTROL_RECORD_1_ADDRESS);
    TEST_ASSERT_TRUE(
        BOOT_CONTROL_RECORD_1_ADDRESS + BOOT_FLASH_SECTOR_SIZE <=
        BOOT_CONTROL_BASE_ADDRESS + BOOT_CONTROL_REGION_SIZE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_committed_record_round_trips_and_uncommitted_is_ignored);
    RUN_TEST(test_crc_corruption_and_missing_records_fail_closed);
    RUN_TEST(test_newest_generation_is_selected_across_counter_wrap);
    RUN_TEST(test_equal_generation_with_different_state_is_rejected);
    RUN_TEST(test_update_requires_other_slot_and_newer_security_counter);
    RUN_TEST(test_pending_image_gets_three_persisted_attempts_then_rolls_back);
    RUN_TEST(test_invalid_pending_image_rolls_back_without_attempting_it);
    RUN_TEST(test_confirmation_promotes_pending_and_advances_anti_rollback);
    RUN_TEST(test_wrong_image_cannot_confirm_and_invalid_confirmed_enters_recovery);
    RUN_TEST(test_redundant_records_occupy_distinct_flash_sectors);
    return UNITY_END();
}
