#ifndef BOOT_TARGET_H
#define BOOT_TARGET_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "boot_control.h"
#include "boot_image.h"
#include "boot_jump.h"

typedef enum
{
    BOOT_TARGET_RESULT_OK = 0U,
    BOOT_TARGET_RESULT_INVALID_ARGUMENT,
    BOOT_TARGET_RESULT_CONTROL_READ_FAILED,
    BOOT_TARGET_RESULT_CONTROL_INVALID,
    BOOT_TARGET_RESULT_CONTROL_WRITE_FAILED,
    BOOT_TARGET_RESULT_RECOVERY,
    BOOT_TARGET_RESULT_JUMP_PREPARE_FAILED,
    BOOT_TARGET_RESULT_JUMP_RETURNED
} Boot_TargetResult_t;

typedef uint8_t (*Boot_TargetReadFn)(void *context,
                                     uint32_t address,
                                     uint8_t *data,
                                     uint32_t length);
typedef Boot_ImageResult_t (*Boot_TargetValidateFn)(
    void *context,
    const Boot_ImageSlot_t *slot,
    uint32_t minimum_security_counter,
    Boot_ImageInfo_t *image_info);
typedef uint8_t (*Boot_TargetPersistFn)(void *context,
                                        const Boot_ControlState_t *state,
                                        uint8_t target_record);
typedef Boot_JumpResult_t (*Boot_TargetJumpFn)(
    void *context,
    const Boot_JumpPlan_t *plan);

typedef struct
{
    Boot_TargetReadFn read;
    Boot_TargetValidateFn validate_image;
    Boot_TargetPersistFn persist_control;
    Boot_TargetJumpFn jump;
    void *context;
} Boot_TargetPlatform_t;

typedef struct
{
    Boot_ControlResult_t control_result;
    Boot_ImageResult_t slot_a_image_result;
    Boot_ImageResult_t slot_b_image_result;
    Boot_Decision_t decision;
    Boot_JumpResult_t jump_result;
    uint8_t selected_control_record;
    uint8_t state_persisted;
} Boot_TargetEvidence_t;

/* A successful call transfers control and therefore does not return. */
Boot_TargetResult_t Boot_Target_Run(
    const Boot_TargetPlatform_t *platform,
    Boot_TargetEvidence_t *evidence
);

#endif /* BOOT_TARGET_H */
