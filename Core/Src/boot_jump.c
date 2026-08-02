#include "boot_jump.h"

#include <stddef.h>

static uint8_t Boot_Jump_IsRangeValid(uint32_t start,
                                      uint32_t length,
                                      uint32_t region_start,
                                      uint32_t region_size)
{
    uint32_t offset;

    if ((length == 0U) || (start < region_start))
    {
        return 0U;
    }

    offset = start - region_start;
    return ((offset <= region_size) &&
            (length <= (region_size - offset))) ? 1U : 0U;
}

Boot_JumpResult_t Boot_Jump_Prepare(
    const Boot_ImageSlot_t *slot,
    const Boot_ImageInfo_t *image_info,
    Boot_JumpPlan_t *plan)
{
    uint32_t expected_image_address;

    if (plan != NULL)
    {
        *plan = (Boot_JumpPlan_t){0};
    }

    if ((slot == NULL) || (image_info == NULL) || (plan == NULL) ||
        (slot->region_size < BOOT_IMAGE_HEADER_SIZE))
    {
        return BOOT_JUMP_RESULT_INVALID_ARGUMENT;
    }

    if (slot->base_address > (UINT32_MAX - BOOT_IMAGE_HEADER_SIZE))
    {
        return BOOT_JUMP_RESULT_IMAGE_MISMATCH;
    }

    expected_image_address = slot->base_address + BOOT_IMAGE_HEADER_SIZE;
    if ((image_info->image_address != expected_image_address) ||
        (image_info->vector_address != expected_image_address) ||
        ((image_info->vector_address % BOOT_IMAGE_VECTOR_ALIGNMENT) != 0U) ||
        ((image_info->initial_stack_pointer & 0x7U) != 0U) ||
        ((image_info->entry_address & 1U) == 0U) ||
        (Boot_Jump_IsRangeValid(image_info->image_address,
                                image_info->image_size,
                                slot->base_address,
                                slot->region_size) == 0U) ||
        (Boot_Jump_IsRangeValid(
            image_info->entry_address & ~(uint32_t)1U,
            2U,
            image_info->image_address,
            image_info->image_size) == 0U))
    {
        return BOOT_JUMP_RESULT_IMAGE_MISMATCH;
    }

    plan->vector_address = image_info->vector_address;
    plan->initial_stack_pointer = image_info->initial_stack_pointer;
    plan->entry_address = image_info->entry_address;
    return BOOT_JUMP_RESULT_OK;
}

Boot_JumpResult_t Boot_Jump_Execute(
    const Boot_JumpPlan_t *plan,
    const Boot_JumpOperations_t *operations)
{
    if ((plan == NULL) || (operations == NULL) ||
        (plan->vector_address == 0U) ||
        (plan->initial_stack_pointer == 0U) ||
        ((plan->entry_address & 1U) == 0U) ||
        (operations->quiesce_peripherals == NULL) ||
        (operations->mask_interrupts == NULL) ||
        (operations->stop_systick == NULL) ||
        (operations->clear_interrupt_state == NULL) ||
        (operations->disable_caches == NULL) ||
        (operations->set_vector_table == NULL) ||
        (operations->branch == NULL) || (operations->halt == NULL))
    {
        return BOOT_JUMP_RESULT_INVALID_ARGUMENT;
    }

    if (operations->quiesce_peripherals(operations->context) == 0U)
    {
        return BOOT_JUMP_RESULT_QUIESCE_FAILED;
    }
    operations->mask_interrupts(operations->context);
    operations->stop_systick(operations->context);
    operations->clear_interrupt_state(operations->context);
    operations->disable_caches(operations->context);
    operations->set_vector_table(operations->context,
                                 plan->vector_address);
    operations->branch(operations->context,
                       plan->initial_stack_pointer,
                       plan->entry_address);

    /* Returning from the reset-handler branch is a platform contract fault. */
    operations->halt(operations->context);
    return BOOT_JUMP_RESULT_BRANCH_RETURNED;
}
