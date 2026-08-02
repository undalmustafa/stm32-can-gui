#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "boot_control.h"
#include "boot_memory_map.h"

typedef enum
{
    BOOT_FLASH_RESULT_OK = 0U,
    BOOT_FLASH_RESULT_INVALID_ARGUMENT,
    BOOT_FLASH_RESULT_INVALID_STATE,
    BOOT_FLASH_RESULT_UNLOCK_FAILED,
    BOOT_FLASH_RESULT_ERASE_FAILED,
    BOOT_FLASH_RESULT_ERASE_VERIFY_FAILED,
    BOOT_FLASH_RESULT_BODY_PROGRAM_FAILED,
    BOOT_FLASH_RESULT_BODY_VERIFY_FAILED,
    BOOT_FLASH_RESULT_COMMIT_PROGRAM_FAILED,
    BOOT_FLASH_RESULT_FINAL_VERIFY_FAILED,
    BOOT_FLASH_RESULT_LOCK_FAILED
} Boot_FlashResult_t;

typedef uint8_t (*Boot_FlashOperationFn)(void *context);
typedef uint8_t (*Boot_FlashEraseSectorFn)(void *context,
                                           uint32_t sector_address);
typedef uint8_t (*Boot_FlashProgramUnitFn)(
    void *context,
    uint32_t address,
    const uint8_t data[BOOT_FLASH_PROGRAM_UNIT_SIZE]);
typedef uint8_t (*Boot_FlashReadFn)(void *context,
                                    uint32_t address,
                                    uint8_t *data,
                                    uint32_t length);

typedef struct
{
    Boot_FlashOperationFn unlock;
    Boot_FlashOperationFn lock;
    Boot_FlashEraseSectorFn erase_sector;
    Boot_FlashProgramUnitFn program_unit;
    Boot_FlashReadFn read;
    void *context;
} Boot_FlashBackend_t;

typedef struct
{
    uint32_t record_address;
    uint8_t target_record;
    uint8_t body_units_programmed;
    uint8_t commit_programmed;
    uint8_t locked;
} Boot_FlashEvidence_t;

/*
 * Erases only the selected redundant record sector. The final 16-byte unit,
 * containing CRC and commit marker, is programmed only after body readback.
 */
Boot_FlashResult_t Boot_Flash_PersistControl(
    const Boot_FlashBackend_t *backend,
    const Boot_ControlState_t *state,
    uint8_t target_record,
    Boot_FlashEvidence_t *evidence
);

/* Populate a backend using STM32H7A3 HAL flash operations. */
void Boot_Flash_Stm32Init(Boot_FlashBackend_t *backend);

#endif /* BOOT_FLASH_H */
