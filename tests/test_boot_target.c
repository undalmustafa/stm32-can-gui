#include "unity.h"

#include "boot_target.h"

#include <string.h>

typedef struct
{
    uint8_t record_0[BOOT_CONTROL_RECORD_SIZE];
    uint8_t record_1[BOOT_CONTROL_RECORD_SIZE];
    Boot_ImageInfo_t slot_a_info;
    Boot_ImageInfo_t slot_b_info;
    Boot_ImageResult_t slot_a_result;
    Boot_ImageResult_t slot_b_result;
    Boot_ControlState_t persisted_state;
    Boot_JumpPlan_t jump_plan;
    uint8_t persist_succeeds;
    uint8_t persist_calls;
    uint8_t persisted_record;
    uint8_t jump_calls;
} TargetFixture_t;

static TargetFixture_t fixture;
static Boot_TargetPlatform_t platform;
static Boot_TargetEvidence_t evidence;

static void PrepareCommitted(const Boot_ControlState_t *state,
                             uint8_t record[BOOT_CONTROL_RECORD_SIZE])
{
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_PrepareRecord(state, record));
    Boot_Control_MarkRecordCommitted(record);
}

static uint8_t ReadControl(void *context,
                           uint32_t address,
                           uint8_t *data,
                           uint32_t length)
{
    TargetFixture_t *target_fixture = (TargetFixture_t *)context;

    if ((data == NULL) || (length != BOOT_CONTROL_RECORD_SIZE))
    {
        return 0U;
    }
    if (address == BOOT_CONTROL_RECORD_0_ADDRESS)
    {
        (void)memcpy(data, target_fixture->record_0, length);
        return 1U;
    }
    if (address == BOOT_CONTROL_RECORD_1_ADDRESS)
    {
        (void)memcpy(data, target_fixture->record_1, length);
        return 1U;
    }
    return 0U;
}

static Boot_ImageResult_t ValidateImage(
    void *context,
    const Boot_ImageSlot_t *slot,
    uint32_t minimum_security_counter,
    Boot_ImageInfo_t *image_info)
{
    TargetFixture_t *target_fixture = (TargetFixture_t *)context;

    TEST_ASSERT_EQUAL_UINT32(5U, minimum_security_counter);
    if (slot->base_address == BOOT_SLOT_A_BASE_ADDRESS)
    {
        *image_info = target_fixture->slot_a_info;
        return target_fixture->slot_a_result;
    }
    *image_info = target_fixture->slot_b_info;
    return target_fixture->slot_b_result;
}

static uint8_t PersistControl(void *context,
                              const Boot_ControlState_t *state,
                              uint8_t target_record)
{
    TargetFixture_t *target_fixture = (TargetFixture_t *)context;

    target_fixture->persist_calls++;
    target_fixture->persisted_state = *state;
    target_fixture->persisted_record = target_record;
    return target_fixture->persist_succeeds;
}

static Boot_JumpResult_t Jump(void *context, const Boot_JumpPlan_t *plan)
{
    TargetFixture_t *target_fixture = (TargetFixture_t *)context;

    target_fixture->jump_calls++;
    target_fixture->jump_plan = *plan;
    return BOOT_JUMP_RESULT_OK;
}

void setUp(void)
{
    Boot_ControlState_t state;

    (void)memset(&fixture, 0, sizeof(fixture));
    (void)memset(fixture.record_0, 0xFF, sizeof(fixture.record_0));
    (void)memset(fixture.record_1, 0xFF, sizeof(fixture.record_1));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Initialize(&state, BOOT_SLOT_A, 5U, 100U));
    PrepareCommitted(&state, fixture.record_0);

    fixture.slot_a_result = BOOT_IMAGE_RESULT_OK;
    fixture.slot_a_info.image_address =
        BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE;
    fixture.slot_a_info.image_size = 0x1000U;
    fixture.slot_a_info.vector_address = fixture.slot_a_info.image_address;
    fixture.slot_a_info.entry_address = fixture.slot_a_info.image_address + 0x101U;
    fixture.slot_a_info.initial_stack_pointer = 0x24001000U;
    fixture.slot_a_info.security_counter = 5U;
    fixture.slot_a_info.build_version = 100U;

    fixture.slot_b_result = BOOT_IMAGE_RESULT_OK;
    fixture.slot_b_info.image_address =
        BOOT_SLOT_B_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE;
    fixture.slot_b_info.image_size = 0x1000U;
    fixture.slot_b_info.vector_address = fixture.slot_b_info.image_address;
    fixture.slot_b_info.entry_address = fixture.slot_b_info.image_address + 0x101U;
    fixture.slot_b_info.initial_stack_pointer = 0x24002000U;
    fixture.slot_b_info.security_counter = 6U;
    fixture.slot_b_info.build_version = 200U;
    fixture.persist_succeeds = 1U;

    platform.read = ReadControl;
    platform.validate_image = ValidateImage;
    platform.persist_control = PersistControl;
    platform.jump = Jump;
    platform.context = &fixture;
}

void tearDown(void)
{
}

static void test_confirmed_image_is_validated_and_handed_off(void)
{
    TEST_ASSERT_EQUAL(
        BOOT_TARGET_RESULT_JUMP_RETURNED,
        Boot_Target_Run(&platform, &evidence));
    TEST_ASSERT_EQUAL(BOOT_DECISION_SLOT_A, evidence.decision);
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.persist_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.jump_calls);
    TEST_ASSERT_EQUAL_HEX32(fixture.slot_a_info.vector_address,
                           fixture.jump_plan.vector_address);
}

static void test_pending_attempt_is_persisted_before_slot_b_jump(void)
{
    Boot_ControlState_t state;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Initialize(&state, BOOT_SLOT_A, 5U, 100U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    PrepareCommitted(&state, fixture.record_0);

    TEST_ASSERT_EQUAL(
        BOOT_TARGET_RESULT_JUMP_RETURNED,
        Boot_Target_Run(&platform, &evidence));
    TEST_ASSERT_EQUAL(BOOT_DECISION_SLOT_B, evidence.decision);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.persist_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.persisted_record);
    TEST_ASSERT_EQUAL_UINT8(
        BOOT_CONTROL_MAX_PENDING_ATTEMPTS - 1U,
        fixture.persisted_state.attempts_remaining);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.jump_calls);
    TEST_ASSERT_EQUAL_HEX32(fixture.slot_b_info.vector_address,
                           fixture.jump_plan.vector_address);
}

static void test_state_identity_mismatch_rolls_back_to_confirmed_image(void)
{
    Boot_ControlState_t state;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Initialize(&state, BOOT_SLOT_A, 5U, 100U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    PrepareCommitted(&state, fixture.record_0);
    fixture.slot_b_info.build_version = 201U;

    TEST_ASSERT_EQUAL(
        BOOT_TARGET_RESULT_JUMP_RETURNED,
        Boot_Target_Run(&platform, &evidence));
    TEST_ASSERT_EQUAL(BOOT_DECISION_SLOT_A, evidence.decision);
    TEST_ASSERT_EQUAL(BOOT_SLOT_NONE,
                      fixture.persisted_state.pending_slot);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.persist_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.jump_calls);
}

static void test_state_write_failure_prevents_pending_image_jump(void)
{
    Boot_ControlState_t state;

    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_Initialize(&state, BOOT_SLOT_A, 5U, 100U));
    TEST_ASSERT_EQUAL(
        BOOT_CONTROL_RESULT_OK,
        Boot_Control_ScheduleUpdate(&state, BOOT_SLOT_B, 6U, 200U));
    PrepareCommitted(&state, fixture.record_0);
    fixture.persist_succeeds = 0U;

    TEST_ASSERT_EQUAL(
        BOOT_TARGET_RESULT_CONTROL_WRITE_FAILED,
        Boot_Target_Run(&platform, &evidence));
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.persist_calls);
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.jump_calls);
}

static void test_invalid_control_or_confirmed_image_enters_recovery(void)
{
    fixture.record_0[0] = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_TARGET_RESULT_CONTROL_INVALID,
        Boot_Target_Run(&platform, &evidence));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.jump_calls);

    setUp();
    fixture.slot_a_result = BOOT_IMAGE_RESULT_DIGEST_INVALID;
    TEST_ASSERT_EQUAL(
        BOOT_TARGET_RESULT_RECOVERY,
        Boot_Target_Run(&platform, &evidence));
    TEST_ASSERT_EQUAL(BOOT_DECISION_RECOVERY, evidence.decision);
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.jump_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_confirmed_image_is_validated_and_handed_off);
    RUN_TEST(test_pending_attempt_is_persisted_before_slot_b_jump);
    RUN_TEST(test_state_identity_mismatch_rolls_back_to_confirmed_image);
    RUN_TEST(test_state_write_failure_prevents_pending_image_jump);
    RUN_TEST(test_invalid_control_or_confirmed_image_enters_recovery);
    return UNITY_END();
}
