#include "boot_flash.h"

#include <stddef.h>
#include <string.h>

#include "stm32h7xx_hal.h"

_Static_assert(FLASH_NB_32BITWORD_IN_FLASHWORD == 4U,
               "STM32H7A3 flash word must be 128 bits");
_Static_assert(BOOT_FLASH_PROGRAM_UNIT_SIZE ==
                   (FLASH_NB_32BITWORD_IN_FLASHWORD * sizeof(uint32_t)),
               "boot backend and HAL flash word sizes differ");

static uint8_t boot_flash_stm32_unlocked;

static uint8_t Boot_Flash_Stm32Unlock(void *context)
{
    HAL_StatusTypeDef status;

    (void)context;
    status = HAL_FLASH_Unlock();
    boot_flash_stm32_unlocked = (status == HAL_OK) ? 1U : 0U;
    return boot_flash_stm32_unlocked;
}

static uint8_t Boot_Flash_Stm32Lock(void *context)
{
    HAL_StatusTypeDef status;

    (void)context;
    status = HAL_FLASH_Lock();
    boot_flash_stm32_unlocked = 0U;
    return (status == HAL_OK) ? 1U : 0U;
}

static uint8_t Boot_Flash_Stm32EraseSector(void *context,
                                          uint32_t sector_address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = UINT32_MAX;
    uint32_t bank_base;

    (void)context;
    if ((sector_address < BOOT_SLOT_A_BASE_ADDRESS) ||
        (sector_address >= BOOT_FLASH_END_ADDRESS) ||
        ((sector_address % BOOT_FLASH_SECTOR_SIZE) != 0U))
    {
        return 0U;
    }

    if (sector_address < BOOT_SLOT_B_BASE_ADDRESS)
    {
        erase.Banks = FLASH_BANK_1;
        bank_base = BOOT_FLASH_BASE_ADDRESS;
    }
    else
    {
        erase.Banks = FLASH_BANK_2;
        bank_base = BOOT_SLOT_B_BASE_ADDRESS;
    }
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = (sector_address - bank_base) /
        BOOT_FLASH_SECTOR_SIZE;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    return ((HAL_FLASHEx_Erase(&erase, &sector_error) == HAL_OK) &&
            (sector_error == UINT32_MAX)) ? 1U : 0U;
}

static uint8_t Boot_Flash_Stm32ProgramUnit(
    void *context,
    uint32_t address,
    const uint8_t data[BOOT_FLASH_PROGRAM_UNIT_SIZE])
{
    uint32_t flash_word[FLASH_NB_32BITWORD_IN_FLASHWORD];

    (void)context;
    if ((data == NULL) ||
        (address < BOOT_SLOT_A_BASE_ADDRESS) ||
        (address > (BOOT_FLASH_END_ADDRESS -
                    BOOT_FLASH_PROGRAM_UNIT_SIZE)) ||
        ((address % BOOT_FLASH_PROGRAM_UNIT_SIZE) != 0U))
    {
        return 0U;
    }

    (void)memcpy(flash_word, data, sizeof(flash_word));
    return (HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_FLASHWORD,
                address,
                (uint32_t)(uintptr_t)&flash_word[0]) == HAL_OK) ? 1U : 0U;
}

static uint8_t Boot_Flash_Stm32CheckReadable(uint32_t address,
                                             uint32_t length)
{
    FLASH_CRCInitTypeDef crc_config = {0};
    uint32_t crc_result;
    uint32_t range_end = address + length - 1U;
    uint8_t lock_after_check = 0U;
    HAL_StatusTypeDef crc_status;

    crc_config.TypeCRC = FLASH_CRC_ADDR;
    crc_config.BurstSize = FLASH_CRC_BURST_SIZE_4;
    crc_config.CRCStartAddr =
        address & ~(BOOT_FLASH_PROGRAM_UNIT_SIZE - 1U);
    crc_config.CRCEndAddr =
        range_end | (BOOT_FLASH_PROGRAM_UNIT_SIZE - 1U);
    if (address < BOOT_SLOT_B_BASE_ADDRESS)
    {
        if (crc_config.CRCEndAddr >= BOOT_SLOT_B_BASE_ADDRESS)
        {
            return 0U;
        }
        crc_config.Bank = FLASH_BANK_1;
    }
    else
    {
        crc_config.Bank = FLASH_BANK_2;
    }

    if (boot_flash_stm32_unlocked == 0U)
    {
        if (HAL_FLASH_Unlock() != HAL_OK)
        {
            return 0U;
        }
        lock_after_check = 1U;
    }

    crc_status = HAL_FLASHEx_ComputeCRC(&crc_config, &crc_result);
    if (lock_after_check != 0U)
    {
        if (HAL_FLASH_Lock() != HAL_OK)
        {
            return 0U;
        }
    }
    return (crc_status == HAL_OK) ? 1U : 0U;
}

static uint8_t Boot_Flash_Stm32Read(void *context,
                                   uint32_t address,
                                   uint8_t *data,
                                   uint32_t length)
{
    (void)context;
    if ((data == NULL) || (length == 0U) ||
        (address < BOOT_FLASH_BASE_ADDRESS) ||
        (address > BOOT_FLASH_END_ADDRESS) ||
        (length > (BOOT_FLASH_END_ADDRESS - address)))
    {
        return 0U;
    }

    /* Hardware CRC reports CRCRDERR without issuing a CPU data-read fault. */
    if (Boot_Flash_Stm32CheckReadable(address, length) == 0U)
    {
        return 0U;
    }

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr(
        (void *)(uintptr_t)(address & ~(uint32_t)0x1FU),
        (int32_t)(((length + (address & 0x1FU) + 31U) / 32U) * 32U));
#endif
    (void)memcpy(data, (const void *)(uintptr_t)address, length);
    return 1U;
}

void Boot_Flash_Stm32Init(Boot_FlashBackend_t *backend)
{
    if (backend != NULL)
    {
        boot_flash_stm32_unlocked = 0U;
        backend->unlock = Boot_Flash_Stm32Unlock;
        backend->lock = Boot_Flash_Stm32Lock;
        backend->erase_sector = Boot_Flash_Stm32EraseSector;
        backend->program_unit = Boot_Flash_Stm32ProgramUnit;
        backend->read = Boot_Flash_Stm32Read;
        backend->context = NULL;
    }
}
