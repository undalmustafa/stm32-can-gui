#include "boot_target.h"

#include <stddef.h>

#include "boot_memory_map.h"

static uint8_t Boot_Target_ImageMatchesState(
    Boot_Slot_t slot,
    const Boot_ControlState_t *state,
    const Boot_ImageInfo_t *image_info)
{
    if (slot == state->confirmed_slot)
    {
        return ((image_info->security_counter ==
                 state->confirmed_security_counter) &&
                (image_info->build_version ==
                 state->confirmed_build_version)) ? 1U : 0U;
    }

    if (slot == state->pending_slot)
    {
        return ((image_info->security_counter ==
                 state->pending_security_counter) &&
                (image_info->build_version ==
                 state->pending_build_version)) ? 1U : 0U;
    }

    return 0U;
}

Boot_TargetResult_t Boot_Target_Run(
    const Boot_TargetPlatform_t *platform,
    Boot_TargetEvidence_t *evidence)
{
    static const Boot_ImageSlot_t slot_a = {
        BOOT_SLOT_A_BASE_ADDRESS,
        BOOT_SLOT_A_REGION_SIZE
    };
    static const Boot_ImageSlot_t slot_b = {
        BOOT_SLOT_B_BASE_ADDRESS,
        BOOT_SLOT_B_REGION_SIZE
    };
    uint8_t record_0[BOOT_CONTROL_RECORD_SIZE];
    uint8_t record_1[BOOT_CONTROL_RECORD_SIZE];
    Boot_ControlState_t state;
    Boot_ImageInfo_t slot_a_info;
    Boot_ImageInfo_t slot_b_info;
    const Boot_ImageSlot_t *selected_slot;
    const Boot_ImageInfo_t *selected_info;
    Boot_JumpPlan_t jump_plan;
    uint8_t slot_a_valid;
    uint8_t slot_b_valid;
    uint8_t state_changed;

    if (evidence != NULL)
    {
        *evidence = (Boot_TargetEvidence_t){0};
        evidence->selected_control_record = UINT8_MAX;
    }

    if ((platform == NULL) || (evidence == NULL) ||
        (platform->read == NULL) ||
        (platform->validate_image == NULL) ||
        (platform->persist_control == NULL) ||
        (platform->jump == NULL))
    {
        return BOOT_TARGET_RESULT_INVALID_ARGUMENT;
    }

    if ((platform->read(platform->context,
                        BOOT_CONTROL_RECORD_0_ADDRESS,
                        record_0,
                        sizeof(record_0)) == 0U) ||
        (platform->read(platform->context,
                        BOOT_CONTROL_RECORD_1_ADDRESS,
                        record_1,
                        sizeof(record_1)) == 0U))
    {
        return BOOT_TARGET_RESULT_CONTROL_READ_FAILED;
    }

    evidence->control_result = Boot_Control_Load(
        record_0,
        record_1,
        &state,
        &evidence->selected_control_record);
    if (evidence->control_result != BOOT_CONTROL_RESULT_OK)
    {
        return BOOT_TARGET_RESULT_CONTROL_INVALID;
    }

    evidence->slot_a_image_result = platform->validate_image(
        platform->context,
        &slot_a,
        state.minimum_security_counter,
        &slot_a_info);
    evidence->slot_b_image_result = platform->validate_image(
        platform->context,
        &slot_b,
        state.minimum_security_counter,
        &slot_b_info);
    slot_a_valid = ((evidence->slot_a_image_result ==
                     BOOT_IMAGE_RESULT_OK) &&
                    (Boot_Target_ImageMatchesState(
                        BOOT_SLOT_A, &state, &slot_a_info) != 0U)) ? 1U : 0U;
    slot_b_valid = ((evidence->slot_b_image_result ==
                     BOOT_IMAGE_RESULT_OK) &&
                    (Boot_Target_ImageMatchesState(
                        BOOT_SLOT_B, &state, &slot_b_info) != 0U)) ? 1U : 0U;

    evidence->control_result = Boot_Control_SelectBoot(
        &state,
        slot_a_valid,
        slot_b_valid,
        &evidence->decision,
        &state_changed);
    if (evidence->control_result != BOOT_CONTROL_RESULT_OK)
    {
        return BOOT_TARGET_RESULT_CONTROL_INVALID;
    }

    if (state_changed != 0U)
    {
        uint8_t target_record;

        target_record = (evidence->selected_control_record == 0U) ? 1U : 0U;
        if (platform->persist_control(platform->context,
                                      &state,
                                      target_record) == 0U)
        {
            return BOOT_TARGET_RESULT_CONTROL_WRITE_FAILED;
        }
        evidence->selected_control_record = target_record;
        evidence->state_persisted = 1U;
    }

    if (evidence->decision == BOOT_DECISION_RECOVERY)
    {
        return BOOT_TARGET_RESULT_RECOVERY;
    }
    if (evidence->decision == BOOT_DECISION_SLOT_A)
    {
        selected_slot = &slot_a;
        selected_info = &slot_a_info;
    }
    else
    {
        selected_slot = &slot_b;
        selected_info = &slot_b_info;
    }

    evidence->jump_result = Boot_Jump_Prepare(
        selected_slot, selected_info, &jump_plan);
    if (evidence->jump_result != BOOT_JUMP_RESULT_OK)
    {
        return BOOT_TARGET_RESULT_JUMP_PREPARE_FAILED;
    }

    evidence->jump_result = platform->jump(platform->context, &jump_plan);
    return BOOT_TARGET_RESULT_JUMP_RETURNED;
}
