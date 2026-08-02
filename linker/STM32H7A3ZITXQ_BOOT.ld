/* 128 KiB immutable bootloader region. */
__firmware_flash_origin__ = 0x08000000;
__firmware_flash_length__ = 0x00020000;

INCLUDE STM32H7A3ZITXQ_FLASH.ld

ASSERT(ORIGIN(FLASH) == 0x08000000,
       "bootloader origin does not match boot_memory_map.h")
ASSERT((ORIGIN(FLASH) + LENGTH(FLASH)) == 0x08020000,
       "bootloader overlaps application slot A")
