#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "boot_image.h"

typedef enum
{
    BOOT_JUMP_RESULT_OK = 0U,
    BOOT_JUMP_RESULT_INVALID_ARGUMENT,
    BOOT_JUMP_RESULT_IMAGE_MISMATCH,
    BOOT_JUMP_RESULT_QUIESCE_FAILED,
    BOOT_JUMP_RESULT_BRANCH_RETURNED
} Boot_JumpResult_t;

typedef struct
{
    uint32_t vector_address;
    uint32_t initial_stack_pointer;
    uint32_t entry_address;
} Boot_JumpPlan_t;

typedef void (*Boot_JumpOperationFn)(void *context);
typedef uint8_t (*Boot_JumpQuiesceFn)(void *context);
typedef void (*Boot_JumpSetVectorFn)(void *context,
                                     uint32_t vector_address);
typedef void (*Boot_JumpBranchFn)(void *context,
                                  uint32_t initial_stack_pointer,
                                  uint32_t entry_address);

typedef struct
{
    Boot_JumpQuiesceFn quiesce_peripherals;
    Boot_JumpOperationFn mask_interrupts;
    Boot_JumpOperationFn stop_systick;
    Boot_JumpOperationFn clear_interrupt_state;
    Boot_JumpOperationFn disable_caches;
    Boot_JumpSetVectorFn set_vector_table;
    Boot_JumpBranchFn branch;
    Boot_JumpOperationFn halt;
    void *context;
} Boot_JumpOperations_t;

Boot_JumpResult_t Boot_Jump_Prepare(
    const Boot_ImageSlot_t *slot,
    const Boot_ImageInfo_t *image_info,
    Boot_JumpPlan_t *plan
);

/* A successful target branch never returns. */
Boot_JumpResult_t Boot_Jump_Execute(
    const Boot_JumpPlan_t *plan,
    const Boot_JumpOperations_t *operations
);

/* Cortex-M7 backend. The quiesce callback must stop owned peripherals/DMA. */
Boot_JumpResult_t Boot_Jump_Stm32(
    const Boot_JumpPlan_t *plan,
    Boot_JumpQuiesceFn quiesce_peripherals,
    void *context
);

#endif /* BOOT_JUMP_H */
