/* Slot B manifest starts at 0x08100000; linked payload starts after 1 KiB. */
__image_slot_base__ = 0x08100000;
__image_manifest_start__ = __image_slot_base__;
__firmware_flash_origin__ = 0x08100400;
__firmware_flash_length__ = 0x000DFC00;

INCLUDE STM32H7A3ZITXQ_FLASH.ld

ASSERT((__firmware_flash_origin__ - __image_slot_base__) == 0x400,
       "slot B manifest header must remain 1 KiB")
ASSERT((ORIGIN(FLASH) + LENGTH(FLASH)) == 0x081E0000,
       "slot B payload overlaps boot-control storage")
