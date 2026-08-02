#ifndef BOOT_CONTROL_H
#define BOOT_CONTROL_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#define BOOT_CONTROL_RECORD_MAGIC          0x31544342UL
#define BOOT_CONTROL_RECORD_VERSION        1U
#define BOOT_CONTROL_RECORD_SIZE           64U
#define BOOT_CONTROL_CRC_OFFSET             56U
#define BOOT_CONTROL_COMMIT_OFFSET          60U
#define BOOT_CONTROL_COMMIT_MARKER          0xC04E7A5AUL
#define BOOT_CONTROL_MAX_PENDING_ATTEMPTS   3U

typedef enum
{
    BOOT_SLOT_NONE = 0U,
    BOOT_SLOT_A = 1U,
    BOOT_SLOT_B = 2U
} Boot_Slot_t;

typedef enum
{
    BOOT_CONTROL_RESULT_OK = 0U,
    BOOT_CONTROL_RESULT_INVALID_ARGUMENT,
    BOOT_CONTROL_RESULT_INVALID_STATE,
    BOOT_CONTROL_RESULT_NO_VALID_RECORD,
    BOOT_CONTROL_RESULT_RECORD_CONFLICT,
    BOOT_CONTROL_RESULT_SECURITY_ROLLBACK
} Boot_ControlResult_t;

typedef enum
{
    BOOT_DECISION_RECOVERY = 0U,
    BOOT_DECISION_SLOT_A,
    BOOT_DECISION_SLOT_B
} Boot_Decision_t;

typedef struct
{
    uint32_t generation;
    Boot_Slot_t confirmed_slot;
    Boot_Slot_t pending_slot;
    uint8_t attempts_remaining;
    uint32_t minimum_security_counter;
    uint32_t confirmed_security_counter;
    uint32_t pending_security_counter;
    uint32_t confirmed_build_version;
    uint32_t pending_build_version;
} Boot_ControlState_t;

Boot_ControlResult_t Boot_Control_Initialize(
    Boot_ControlState_t *state,
    Boot_Slot_t confirmed_slot,
    uint32_t security_counter,
    uint32_t build_version
);

Boot_ControlResult_t Boot_Control_PrepareRecord(
    const Boot_ControlState_t *state,
    uint8_t record[BOOT_CONTROL_RECORD_SIZE]
);

/* Models the final flash-word write. Program this word only after 0..59. */
void Boot_Control_MarkRecordCommitted(
    uint8_t record[BOOT_CONTROL_RECORD_SIZE]
);

Boot_ControlResult_t Boot_Control_Load(
    const uint8_t record_0[BOOT_CONTROL_RECORD_SIZE],
    const uint8_t record_1[BOOT_CONTROL_RECORD_SIZE],
    Boot_ControlState_t *state,
    uint8_t *selected_record
);

Boot_ControlResult_t Boot_Control_ScheduleUpdate(
    Boot_ControlState_t *state,
    Boot_Slot_t target_slot,
    uint32_t security_counter,
    uint32_t build_version
);

Boot_ControlResult_t Boot_Control_SelectBoot(
    Boot_ControlState_t *state,
    uint8_t slot_a_valid,
    uint8_t slot_b_valid,
    Boot_Decision_t *decision,
    uint8_t *state_changed
);

Boot_ControlResult_t Boot_Control_ConfirmRunning(
    Boot_ControlState_t *state,
    Boot_Slot_t running_slot,
    uint32_t security_counter,
    uint32_t build_version,
    uint8_t *state_changed
);

#endif /* BOOT_CONTROL_H */
