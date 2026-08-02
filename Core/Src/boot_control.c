#include "boot_control.h"

#include <stddef.h>

#define BOOT_CONTROL_OFFSET_MAGIC               0U
#define BOOT_CONTROL_OFFSET_VERSION             4U
#define BOOT_CONTROL_OFFSET_SIZE                6U
#define BOOT_CONTROL_OFFSET_GENERATION          8U
#define BOOT_CONTROL_OFFSET_CONFIRMED_SLOT       12U
#define BOOT_CONTROL_OFFSET_PENDING_SLOT         13U
#define BOOT_CONTROL_OFFSET_ATTEMPTS_REMAINING   14U
#define BOOT_CONTROL_OFFSET_RESERVED_BYTE        15U
#define BOOT_CONTROL_OFFSET_MIN_SECURITY         16U
#define BOOT_CONTROL_OFFSET_CONFIRMED_SECURITY   20U
#define BOOT_CONTROL_OFFSET_PENDING_SECURITY     24U
#define BOOT_CONTROL_OFFSET_CONFIRMED_BUILD      28U
#define BOOT_CONTROL_OFFSET_PENDING_BUILD        32U
#define BOOT_CONTROL_OFFSET_FLAGS                36U
#define BOOT_CONTROL_OFFSET_RESERVED_WORDS       40U
#define BOOT_CONTROL_RESERVED_WORD_COUNT         4U

_Static_assert(BOOT_CONTROL_CRC_OFFSET == 56U,
               "boot-control CRC coverage changed");
_Static_assert((BOOT_CONTROL_COMMIT_OFFSET + sizeof(uint32_t)) ==
                   BOOT_CONTROL_RECORD_SIZE,
               "boot-control record must remain 64 bytes");

static uint16_t Boot_Control_ReadU16Le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] |
                      ((uint16_t)data[1] << 8U));
}

static uint32_t Boot_Control_ReadU32Le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void Boot_Control_WriteU16Le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void Boot_Control_WriteU32Le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t Boot_Control_CalculateCrc32(const uint8_t *data,
                                            uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t byte_index;
    uint8_t bit_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static uint8_t Boot_Control_IsSlot(Boot_Slot_t slot)
{
    return ((slot == BOOT_SLOT_A) || (slot == BOOT_SLOT_B)) ? 1U : 0U;
}

static uint8_t Boot_Control_IsStateValid(const Boot_ControlState_t *state)
{
    if ((state == NULL) || (state->generation == 0U) ||
        (Boot_Control_IsSlot(state->confirmed_slot) == 0U) ||
        (state->confirmed_security_counter !=
         state->minimum_security_counter))
    {
        return 0U;
    }

    if (state->pending_slot == BOOT_SLOT_NONE)
    {
        return ((state->attempts_remaining == 0U) &&
                (state->pending_security_counter == 0U) &&
                (state->pending_build_version == 0U)) ? 1U : 0U;
    }

    return ((Boot_Control_IsSlot(state->pending_slot) != 0U) &&
            (state->pending_slot != state->confirmed_slot) &&
            (state->attempts_remaining <=
             BOOT_CONTROL_MAX_PENDING_ATTEMPTS) &&
            (state->pending_security_counter >
             state->minimum_security_counter)) ? 1U : 0U;
}

static uint32_t Boot_Control_NextGeneration(uint32_t generation)
{
    generation++;
    return (generation == 0U) ? 1U : generation;
}

static uint8_t Boot_Control_IsGenerationNewer(uint32_t candidate,
                                              uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1U : 0U;
}

static void Boot_Control_ClearPending(Boot_ControlState_t *state)
{
    state->pending_slot = BOOT_SLOT_NONE;
    state->attempts_remaining = 0U;
    state->pending_security_counter = 0U;
    state->pending_build_version = 0U;
}

static uint8_t Boot_Control_StatesEqual(const Boot_ControlState_t *left,
                                        const Boot_ControlState_t *right)
{
    return ((left->generation == right->generation) &&
            (left->confirmed_slot == right->confirmed_slot) &&
            (left->pending_slot == right->pending_slot) &&
            (left->attempts_remaining == right->attempts_remaining) &&
            (left->minimum_security_counter ==
             right->minimum_security_counter) &&
            (left->confirmed_security_counter ==
             right->confirmed_security_counter) &&
            (left->pending_security_counter ==
             right->pending_security_counter) &&
            (left->confirmed_build_version ==
             right->confirmed_build_version) &&
            (left->pending_build_version ==
             right->pending_build_version)) ? 1U : 0U;
}

static uint8_t Boot_Control_DecodeRecord(
    const uint8_t record[BOOT_CONTROL_RECORD_SIZE],
    Boot_ControlState_t *state)
{
    uint32_t word_index;
    uint32_t expected_crc;

    if ((record == NULL) || (state == NULL) ||
        (Boot_Control_ReadU32Le(
            &record[BOOT_CONTROL_OFFSET_MAGIC]) !=
         BOOT_CONTROL_RECORD_MAGIC) ||
        (Boot_Control_ReadU16Le(
            &record[BOOT_CONTROL_OFFSET_VERSION]) !=
         BOOT_CONTROL_RECORD_VERSION) ||
        (Boot_Control_ReadU16Le(
            &record[BOOT_CONTROL_OFFSET_SIZE]) !=
         BOOT_CONTROL_RECORD_SIZE) ||
        (record[BOOT_CONTROL_OFFSET_RESERVED_BYTE] != 0U) ||
        (Boot_Control_ReadU32Le(&record[BOOT_CONTROL_OFFSET_FLAGS]) != 0U) ||
        (Boot_Control_ReadU32Le(&record[BOOT_CONTROL_COMMIT_OFFSET]) !=
         BOOT_CONTROL_COMMIT_MARKER))
    {
        return 0U;
    }

    for (word_index = 0U;
         word_index < BOOT_CONTROL_RESERVED_WORD_COUNT;
         word_index++)
    {
        if (Boot_Control_ReadU32Le(
                &record[BOOT_CONTROL_OFFSET_RESERVED_WORDS +
                        (word_index * sizeof(uint32_t))]) != 0U)
        {
            return 0U;
        }
    }

    expected_crc = Boot_Control_CalculateCrc32(
        record, BOOT_CONTROL_CRC_OFFSET);
    if (expected_crc != Boot_Control_ReadU32Le(
                            &record[BOOT_CONTROL_CRC_OFFSET]))
    {
        return 0U;
    }

    state->generation = Boot_Control_ReadU32Le(
        &record[BOOT_CONTROL_OFFSET_GENERATION]);
    state->confirmed_slot = (Boot_Slot_t)
        record[BOOT_CONTROL_OFFSET_CONFIRMED_SLOT];
    state->pending_slot = (Boot_Slot_t)
        record[BOOT_CONTROL_OFFSET_PENDING_SLOT];
    state->attempts_remaining =
        record[BOOT_CONTROL_OFFSET_ATTEMPTS_REMAINING];
    state->minimum_security_counter = Boot_Control_ReadU32Le(
        &record[BOOT_CONTROL_OFFSET_MIN_SECURITY]);
    state->confirmed_security_counter = Boot_Control_ReadU32Le(
        &record[BOOT_CONTROL_OFFSET_CONFIRMED_SECURITY]);
    state->pending_security_counter = Boot_Control_ReadU32Le(
        &record[BOOT_CONTROL_OFFSET_PENDING_SECURITY]);
    state->confirmed_build_version = Boot_Control_ReadU32Le(
        &record[BOOT_CONTROL_OFFSET_CONFIRMED_BUILD]);
    state->pending_build_version = Boot_Control_ReadU32Le(
        &record[BOOT_CONTROL_OFFSET_PENDING_BUILD]);
    return Boot_Control_IsStateValid(state);
}

Boot_ControlResult_t Boot_Control_Initialize(
    Boot_ControlState_t *state,
    Boot_Slot_t confirmed_slot,
    uint32_t security_counter,
    uint32_t build_version)
{
    if ((state == NULL) ||
        (Boot_Control_IsSlot(confirmed_slot) == 0U))
    {
        return BOOT_CONTROL_RESULT_INVALID_ARGUMENT;
    }

    *state = (Boot_ControlState_t){0};
    state->generation = 1U;
    state->confirmed_slot = confirmed_slot;
    state->minimum_security_counter = security_counter;
    state->confirmed_security_counter = security_counter;
    state->confirmed_build_version = build_version;
    return BOOT_CONTROL_RESULT_OK;
}

Boot_ControlResult_t Boot_Control_PrepareRecord(
    const Boot_ControlState_t *state,
    uint8_t record[BOOT_CONTROL_RECORD_SIZE])
{
    uint32_t index;
    uint32_t crc;

    if (record == NULL)
    {
        return BOOT_CONTROL_RESULT_INVALID_ARGUMENT;
    }
    if (Boot_Control_IsStateValid(state) == 0U)
    {
        return BOOT_CONTROL_RESULT_INVALID_STATE;
    }

    for (index = 0U; index < BOOT_CONTROL_RECORD_SIZE; index++)
    {
        record[index] = 0U;
    }

    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_MAGIC],
                            BOOT_CONTROL_RECORD_MAGIC);
    Boot_Control_WriteU16Le(&record[BOOT_CONTROL_OFFSET_VERSION],
                            BOOT_CONTROL_RECORD_VERSION);
    Boot_Control_WriteU16Le(&record[BOOT_CONTROL_OFFSET_SIZE],
                            BOOT_CONTROL_RECORD_SIZE);
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_GENERATION],
                            state->generation);
    record[BOOT_CONTROL_OFFSET_CONFIRMED_SLOT] =
        (uint8_t)state->confirmed_slot;
    record[BOOT_CONTROL_OFFSET_PENDING_SLOT] =
        (uint8_t)state->pending_slot;
    record[BOOT_CONTROL_OFFSET_ATTEMPTS_REMAINING] =
        state->attempts_remaining;
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_MIN_SECURITY],
                            state->minimum_security_counter);
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_CONFIRMED_SECURITY],
                            state->confirmed_security_counter);
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_PENDING_SECURITY],
                            state->pending_security_counter);
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_CONFIRMED_BUILD],
                            state->confirmed_build_version);
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_OFFSET_PENDING_BUILD],
                            state->pending_build_version);
    crc = Boot_Control_CalculateCrc32(record, BOOT_CONTROL_CRC_OFFSET);
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_CRC_OFFSET], crc);

    /* Erased marker: flash backend must program commit as its final write. */
    Boot_Control_WriteU32Le(&record[BOOT_CONTROL_COMMIT_OFFSET],
                            UINT32_MAX);
    return BOOT_CONTROL_RESULT_OK;
}

void Boot_Control_MarkRecordCommitted(
    uint8_t record[BOOT_CONTROL_RECORD_SIZE])
{
    if (record != NULL)
    {
        Boot_Control_WriteU32Le(&record[BOOT_CONTROL_COMMIT_OFFSET],
                                BOOT_CONTROL_COMMIT_MARKER);
    }
}

Boot_ControlResult_t Boot_Control_Load(
    const uint8_t record_0[BOOT_CONTROL_RECORD_SIZE],
    const uint8_t record_1[BOOT_CONTROL_RECORD_SIZE],
    Boot_ControlState_t *state,
    uint8_t *selected_record)
{
    Boot_ControlState_t state_0 = {0};
    Boot_ControlState_t state_1 = {0};
    uint8_t valid_0;
    uint8_t valid_1;
    uint32_t generation_delta;

    if ((record_0 == NULL) || (record_1 == NULL) ||
        (state == NULL) || (selected_record == NULL))
    {
        return BOOT_CONTROL_RESULT_INVALID_ARGUMENT;
    }

    *state = (Boot_ControlState_t){0};
    *selected_record = UINT8_MAX;
    valid_0 = Boot_Control_DecodeRecord(record_0, &state_0);
    valid_1 = Boot_Control_DecodeRecord(record_1, &state_1);

    if ((valid_0 == 0U) && (valid_1 == 0U))
    {
        return BOOT_CONTROL_RESULT_NO_VALID_RECORD;
    }
    if (valid_1 == 0U)
    {
        *state = state_0;
        *selected_record = 0U;
        return BOOT_CONTROL_RESULT_OK;
    }
    if (valid_0 == 0U)
    {
        *state = state_1;
        *selected_record = 1U;
        return BOOT_CONTROL_RESULT_OK;
    }

    generation_delta = state_1.generation - state_0.generation;
    if (generation_delta == 0U)
    {
        if (Boot_Control_StatesEqual(&state_0, &state_1) == 0U)
        {
            return BOOT_CONTROL_RESULT_RECORD_CONFLICT;
        }
        *state = state_0;
        *selected_record = 0U;
        return BOOT_CONTROL_RESULT_OK;
    }
    if (generation_delta == 0x80000000UL)
    {
        return BOOT_CONTROL_RESULT_RECORD_CONFLICT;
    }

    if (Boot_Control_IsGenerationNewer(state_1.generation,
                                       state_0.generation) != 0U)
    {
        *state = state_1;
        *selected_record = 1U;
    }
    else
    {
        *state = state_0;
        *selected_record = 0U;
    }
    return BOOT_CONTROL_RESULT_OK;
}

Boot_ControlResult_t Boot_Control_ScheduleUpdate(
    Boot_ControlState_t *state,
    Boot_Slot_t target_slot,
    uint32_t security_counter,
    uint32_t build_version)
{
    if ((state == NULL) || (Boot_Control_IsSlot(target_slot) == 0U))
    {
        return BOOT_CONTROL_RESULT_INVALID_ARGUMENT;
    }
    if ((Boot_Control_IsStateValid(state) == 0U) ||
        (target_slot == state->confirmed_slot) ||
        (state->pending_slot != BOOT_SLOT_NONE))
    {
        return BOOT_CONTROL_RESULT_INVALID_STATE;
    }
    if (security_counter <= state->minimum_security_counter)
    {
        return BOOT_CONTROL_RESULT_SECURITY_ROLLBACK;
    }

    state->pending_slot = target_slot;
    state->attempts_remaining = BOOT_CONTROL_MAX_PENDING_ATTEMPTS;
    state->pending_security_counter = security_counter;
    state->pending_build_version = build_version;
    state->generation = Boot_Control_NextGeneration(state->generation);
    return BOOT_CONTROL_RESULT_OK;
}

Boot_ControlResult_t Boot_Control_SelectBoot(
    Boot_ControlState_t *state,
    uint8_t slot_a_valid,
    uint8_t slot_b_valid,
    Boot_Decision_t *decision,
    uint8_t *state_changed)
{
    uint8_t pending_valid;
    uint8_t confirmed_valid;

    if ((state == NULL) || (decision == NULL) || (state_changed == NULL))
    {
        return BOOT_CONTROL_RESULT_INVALID_ARGUMENT;
    }
    if (Boot_Control_IsStateValid(state) == 0U)
    {
        return BOOT_CONTROL_RESULT_INVALID_STATE;
    }

    *decision = BOOT_DECISION_RECOVERY;
    *state_changed = 0U;
    slot_a_valid = (slot_a_valid != 0U) ? 1U : 0U;
    slot_b_valid = (slot_b_valid != 0U) ? 1U : 0U;
    confirmed_valid = (state->confirmed_slot == BOOT_SLOT_A)
        ? slot_a_valid : slot_b_valid;

    if (state->pending_slot != BOOT_SLOT_NONE)
    {
        pending_valid = (state->pending_slot == BOOT_SLOT_A)
            ? slot_a_valid : slot_b_valid;
        if ((pending_valid != 0U) &&
            (state->attempts_remaining > 0U))
        {
            state->attempts_remaining--;
            state->generation =
                Boot_Control_NextGeneration(state->generation);
            *state_changed = 1U;
            *decision = (state->pending_slot == BOOT_SLOT_A)
                ? BOOT_DECISION_SLOT_A : BOOT_DECISION_SLOT_B;
            return BOOT_CONTROL_RESULT_OK;
        }

        Boot_Control_ClearPending(state);
        state->generation = Boot_Control_NextGeneration(state->generation);
        *state_changed = 1U;
    }

    if (confirmed_valid != 0U)
    {
        *decision = (state->confirmed_slot == BOOT_SLOT_A)
            ? BOOT_DECISION_SLOT_A : BOOT_DECISION_SLOT_B;
    }
    return BOOT_CONTROL_RESULT_OK;
}

Boot_ControlResult_t Boot_Control_ConfirmRunning(
    Boot_ControlState_t *state,
    Boot_Slot_t running_slot,
    uint32_t security_counter,
    uint32_t build_version,
    uint8_t *state_changed)
{
    if ((state == NULL) || (Boot_Control_IsSlot(running_slot) == 0U) ||
        (state_changed == NULL))
    {
        return BOOT_CONTROL_RESULT_INVALID_ARGUMENT;
    }
    if (Boot_Control_IsStateValid(state) == 0U)
    {
        return BOOT_CONTROL_RESULT_INVALID_STATE;
    }

    *state_changed = 0U;
    if ((state->pending_slot == BOOT_SLOT_NONE) &&
        (running_slot == state->confirmed_slot) &&
        (security_counter == state->confirmed_security_counter) &&
        (build_version == state->confirmed_build_version))
    {
        return BOOT_CONTROL_RESULT_OK;
    }

    if ((running_slot != state->pending_slot) ||
        (security_counter != state->pending_security_counter) ||
        (build_version != state->pending_build_version))
    {
        return BOOT_CONTROL_RESULT_INVALID_STATE;
    }

    state->confirmed_slot = running_slot;
    state->minimum_security_counter = security_counter;
    state->confirmed_security_counter = security_counter;
    state->confirmed_build_version = build_version;
    Boot_Control_ClearPending(state);
    state->generation = Boot_Control_NextGeneration(state->generation);
    *state_changed = 1U;
    return BOOT_CONTROL_RESULT_OK;
}
