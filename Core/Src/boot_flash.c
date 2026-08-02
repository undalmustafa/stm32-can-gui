#include "boot_flash.h"

#include <stddef.h>

#define BOOT_FLASH_BODY_SIZE \
    (BOOT_CONTROL_RECORD_SIZE - BOOT_FLASH_PROGRAM_UNIT_SIZE)
#define BOOT_FLASH_BODY_UNIT_COUNT \
    (BOOT_FLASH_BODY_SIZE / BOOT_FLASH_PROGRAM_UNIT_SIZE)

_Static_assert((BOOT_CONTROL_RECORD_SIZE %
                BOOT_FLASH_PROGRAM_UNIT_SIZE) == 0U,
               "boot-control record must use whole flash program units");
_Static_assert(BOOT_CONTROL_COMMIT_OFFSET >= BOOT_FLASH_BODY_SIZE,
               "commit marker must reside in the final flash program unit");
_Static_assert((BOOT_CONTROL_COMMIT_OFFSET + sizeof(uint32_t)) ==
                   BOOT_CONTROL_RECORD_SIZE,
               "commit marker must end the record");
_Static_assert((BOOT_CONTROL_RECORD_0_ADDRESS %
                BOOT_FLASH_PROGRAM_UNIT_SIZE) == 0U,
               "boot-control record 0 must be flashword aligned");
_Static_assert((BOOT_CONTROL_RECORD_1_ADDRESS %
                BOOT_FLASH_PROGRAM_UNIT_SIZE) == 0U,
               "boot-control record 1 must be flashword aligned");

static uint32_t Boot_Flash_RecordAddress(uint8_t target_record)
{
    return (target_record == 0U)
        ? BOOT_CONTROL_RECORD_0_ADDRESS
        : BOOT_CONTROL_RECORD_1_ADDRESS;
}

static uint8_t Boot_Flash_VerifyRange(
    const Boot_FlashBackend_t *backend,
    uint32_t address,
    const uint8_t *expected,
    uint32_t length)
{
    uint8_t readback[BOOT_FLASH_PROGRAM_UNIT_SIZE];
    uint32_t offset;
    uint32_t byte_index;

    for (offset = 0U; offset < length;
         offset += BOOT_FLASH_PROGRAM_UNIT_SIZE)
    {
        if (backend->read(backend->context,
                          address + offset,
                          readback,
                          sizeof(readback)) == 0U)
        {
            return 0U;
        }
        for (byte_index = 0U;
             byte_index < BOOT_FLASH_PROGRAM_UNIT_SIZE;
             byte_index++)
        {
            if (readback[byte_index] != expected[offset + byte_index])
            {
                return 0U;
            }
        }
    }

    return 1U;
}

static uint8_t Boot_Flash_VerifyErasedRecord(
    const Boot_FlashBackend_t *backend,
    uint32_t address)
{
    uint8_t erased[BOOT_CONTROL_RECORD_SIZE];
    uint32_t byte_index;

    for (byte_index = 0U; byte_index < sizeof(erased); byte_index++)
    {
        erased[byte_index] = UINT8_MAX;
    }
    return Boot_Flash_VerifyRange(
        backend, address, erased, sizeof(erased));
}

static Boot_FlashResult_t Boot_Flash_Finish(
    const Boot_FlashBackend_t *backend,
    Boot_FlashResult_t result,
    Boot_FlashEvidence_t *evidence)
{
    if (backend->lock(backend->context) == 0U)
    {
        return BOOT_FLASH_RESULT_LOCK_FAILED;
    }
    evidence->locked = 1U;
    return result;
}

Boot_FlashResult_t Boot_Flash_PersistControl(
    const Boot_FlashBackend_t *backend,
    const Boot_ControlState_t *state,
    uint8_t target_record,
    Boot_FlashEvidence_t *evidence)
{
    uint8_t record[BOOT_CONTROL_RECORD_SIZE];
    uint32_t record_address;
    uint32_t unit_index;
    Boot_ControlResult_t prepare_result;
    Boot_FlashResult_t result;

    if (evidence != NULL)
    {
        *evidence = (Boot_FlashEvidence_t){0};
        evidence->target_record = UINT8_MAX;
    }

    if ((backend == NULL) || (state == NULL) || (evidence == NULL) ||
        (target_record > 1U) || (backend->unlock == NULL) ||
        (backend->lock == NULL) || (backend->erase_sector == NULL) ||
        (backend->program_unit == NULL) || (backend->read == NULL))
    {
        return BOOT_FLASH_RESULT_INVALID_ARGUMENT;
    }

    prepare_result = Boot_Control_PrepareRecord(state, record);
    if (prepare_result != BOOT_CONTROL_RESULT_OK)
    {
        return BOOT_FLASH_RESULT_INVALID_STATE;
    }

    record_address = Boot_Flash_RecordAddress(target_record);
    evidence->record_address = record_address;
    evidence->target_record = target_record;

    if (backend->unlock(backend->context) == 0U)
    {
        return Boot_Flash_Finish(
            backend, BOOT_FLASH_RESULT_UNLOCK_FAILED, evidence);
    }

    result = BOOT_FLASH_RESULT_OK;
    if (backend->erase_sector(backend->context, record_address) == 0U)
    {
        result = BOOT_FLASH_RESULT_ERASE_FAILED;
    }
    else if (Boot_Flash_VerifyErasedRecord(backend, record_address) == 0U)
    {
        result = BOOT_FLASH_RESULT_ERASE_VERIFY_FAILED;
    }

    for (unit_index = 0U;
         (result == BOOT_FLASH_RESULT_OK) &&
         (unit_index < BOOT_FLASH_BODY_UNIT_COUNT);
         unit_index++)
    {
        uint32_t unit_offset =
            unit_index * BOOT_FLASH_PROGRAM_UNIT_SIZE;

        if (backend->program_unit(
                backend->context,
                record_address + unit_offset,
                &record[unit_offset]) == 0U)
        {
            result = BOOT_FLASH_RESULT_BODY_PROGRAM_FAILED;
        }
        else
        {
            evidence->body_units_programmed++;
        }
    }

    if ((result == BOOT_FLASH_RESULT_OK) &&
        (Boot_Flash_VerifyRange(backend,
                                record_address,
                                record,
                                BOOT_FLASH_BODY_SIZE) == 0U))
    {
        result = BOOT_FLASH_RESULT_BODY_VERIFY_FAILED;
    }

    if (result == BOOT_FLASH_RESULT_OK)
    {
        Boot_Control_MarkRecordCommitted(record);
        if (backend->program_unit(
                backend->context,
                record_address + BOOT_FLASH_BODY_SIZE,
                &record[BOOT_FLASH_BODY_SIZE]) == 0U)
        {
            result = BOOT_FLASH_RESULT_COMMIT_PROGRAM_FAILED;
        }
        else
        {
            evidence->commit_programmed = 1U;
        }
    }

    if ((result == BOOT_FLASH_RESULT_OK) &&
        (Boot_Flash_VerifyRange(backend,
                                record_address,
                                record,
                                sizeof(record)) == 0U))
    {
        result = BOOT_FLASH_RESULT_FINAL_VERIFY_FAILED;
    }

    return Boot_Flash_Finish(backend, result, evidence);
}
