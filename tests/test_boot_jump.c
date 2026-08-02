#include "unity.h"

#include "boot_jump.h"
#include "boot_memory_map.h"

#include <string.h>

enum
{
    STEP_QUIESCE = 1,
    STEP_MASK_INTERRUPTS,
    STEP_STOP_SYSTICK,
    STEP_CLEAR_INTERRUPTS,
    STEP_DISABLE_CACHES,
    STEP_SET_VECTOR,
    STEP_BRANCH,
    STEP_HALT
};

typedef struct
{
    uint8_t steps[8];
    uint8_t step_count;
    uint32_t vector_address;
    uint32_t stack_pointer;
    uint32_t entry_address;
    uint8_t quiesce_succeeds;
} JumpTrace_t;

static Boot_ImageSlot_t slot;
static Boot_ImageInfo_t image_info;
static Boot_JumpPlan_t plan;
static Boot_JumpOperations_t operations;
static JumpTrace_t trace;

static void RecordStep(JumpTrace_t *jump_trace, uint8_t step)
{
    TEST_ASSERT_TRUE(jump_trace->step_count < sizeof(jump_trace->steps));
    jump_trace->steps[jump_trace->step_count] = step;
    jump_trace->step_count++;
}

static uint8_t Quiesce(void *context)
{
    JumpTrace_t *jump_trace = (JumpTrace_t *)context;

    RecordStep(jump_trace, STEP_QUIESCE);
    return jump_trace->quiesce_succeeds;
}

static void MaskInterrupts(void *context)
{
    RecordStep((JumpTrace_t *)context, STEP_MASK_INTERRUPTS);
}

static void StopSysTick(void *context)
{
    RecordStep((JumpTrace_t *)context, STEP_STOP_SYSTICK);
}

static void ClearInterrupts(void *context)
{
    RecordStep((JumpTrace_t *)context, STEP_CLEAR_INTERRUPTS);
}

static void DisableCaches(void *context)
{
    RecordStep((JumpTrace_t *)context, STEP_DISABLE_CACHES);
}

static void SetVector(void *context, uint32_t vector_address)
{
    JumpTrace_t *jump_trace = (JumpTrace_t *)context;

    RecordStep(jump_trace, STEP_SET_VECTOR);
    jump_trace->vector_address = vector_address;
}

static void Branch(void *context,
                   uint32_t initial_stack_pointer,
                   uint32_t entry_address)
{
    JumpTrace_t *jump_trace = (JumpTrace_t *)context;

    RecordStep(jump_trace, STEP_BRANCH);
    jump_trace->stack_pointer = initial_stack_pointer;
    jump_trace->entry_address = entry_address;
}

static void Halt(void *context)
{
    RecordStep((JumpTrace_t *)context, STEP_HALT);
}

void setUp(void)
{
    (void)memset(&trace, 0, sizeof(trace));
    trace.quiesce_succeeds = 1U;
    slot.base_address = BOOT_SLOT_A_BASE_ADDRESS;
    slot.region_size = BOOT_SLOT_A_REGION_SIZE;
    image_info = (Boot_ImageInfo_t){0};
    image_info.image_address =
        BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE;
    image_info.image_size = 0x1000U;
    image_info.vector_address = image_info.image_address;
    image_info.entry_address = image_info.image_address + 0x101U;
    image_info.initial_stack_pointer = 0x24001000U;

    operations.quiesce_peripherals = Quiesce;
    operations.mask_interrupts = MaskInterrupts;
    operations.stop_systick = StopSysTick;
    operations.clear_interrupt_state = ClearInterrupts;
    operations.disable_caches = DisableCaches;
    operations.set_vector_table = SetVector;
    operations.branch = Branch;
    operations.halt = Halt;
    operations.context = &trace;
}

void tearDown(void)
{
}

static void test_prepare_accepts_validated_slot_a_and_slot_b_images(void)
{
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_OK,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
    TEST_ASSERT_EQUAL_HEX32(image_info.vector_address,
                           plan.vector_address);
    TEST_ASSERT_EQUAL_HEX32(image_info.initial_stack_pointer,
                           plan.initial_stack_pointer);
    TEST_ASSERT_EQUAL_HEX32(image_info.entry_address,
                           plan.entry_address);

    slot.base_address = BOOT_SLOT_B_BASE_ADDRESS;
    slot.region_size = BOOT_SLOT_B_REGION_SIZE;
    image_info.image_address =
        BOOT_SLOT_B_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE;
    image_info.vector_address = image_info.image_address;
    image_info.entry_address = image_info.image_address + 0x101U;
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_OK,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
}

static void test_prepare_rejects_mismatched_or_unbootable_metadata(void)
{
    image_info.vector_address += BOOT_IMAGE_VECTOR_ALIGNMENT;
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_IMAGE_MISMATCH,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
    TEST_ASSERT_EQUAL_HEX32(0U, plan.vector_address);

    image_info.vector_address = image_info.image_address;
    image_info.entry_address &= ~(uint32_t)1U;
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_IMAGE_MISMATCH,
        Boot_Jump_Prepare(&slot, &image_info, &plan));

    image_info.entry_address = image_info.image_address + 0x101U;
    image_info.image_size = slot.region_size;
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_IMAGE_MISMATCH,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
}

static void test_execute_uses_fail_safe_handoff_order(void)
{
    static const uint8_t expected_steps[] = {
        STEP_QUIESCE,
        STEP_MASK_INTERRUPTS,
        STEP_STOP_SYSTICK,
        STEP_CLEAR_INTERRUPTS,
        STEP_DISABLE_CACHES,
        STEP_SET_VECTOR,
        STEP_BRANCH,
        STEP_HALT
    };

    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_OK,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_BRANCH_RETURNED,
        Boot_Jump_Execute(&plan, &operations));
    TEST_ASSERT_EQUAL_UINT8(sizeof(expected_steps), trace.step_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_steps,
                                  trace.steps,
                                  sizeof(expected_steps));
    TEST_ASSERT_EQUAL_HEX32(plan.vector_address, trace.vector_address);
    TEST_ASSERT_EQUAL_HEX32(plan.initial_stack_pointer,
                           trace.stack_pointer);
    TEST_ASSERT_EQUAL_HEX32(plan.entry_address, trace.entry_address);
}

static void test_execute_rejects_incomplete_platform_without_side_effects(void)
{
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_OK,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
    operations.disable_caches = NULL;
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_INVALID_ARGUMENT,
        Boot_Jump_Execute(&plan, &operations));
    TEST_ASSERT_EQUAL_UINT8(0U, trace.step_count);
}

static void test_quiesce_failure_prevents_interrupt_mask_and_branch(void)
{
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_OK,
        Boot_Jump_Prepare(&slot, &image_info, &plan));
    trace.quiesce_succeeds = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_JUMP_RESULT_QUIESCE_FAILED,
        Boot_Jump_Execute(&plan, &operations));
    TEST_ASSERT_EQUAL_UINT8(1U, trace.step_count);
    TEST_ASSERT_EQUAL_UINT8(STEP_QUIESCE, trace.steps[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_prepare_accepts_validated_slot_a_and_slot_b_images);
    RUN_TEST(test_prepare_rejects_mismatched_or_unbootable_metadata);
    RUN_TEST(test_execute_uses_fail_safe_handoff_order);
    RUN_TEST(test_execute_rejects_incomplete_platform_without_side_effects);
    RUN_TEST(test_quiesce_failure_prevents_interrupt_mask_and_branch);
    return UNITY_END();
}
