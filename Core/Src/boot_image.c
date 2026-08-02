#include "boot_image.h"

#include <stddef.h>

_Static_assert(
    (BOOT_LOADER_BASE_ADDRESS + BOOT_LOADER_REGION_SIZE) ==
        BOOT_SLOT_A_BASE_ADDRESS,
    "bootloader and slot A must be contiguous");
_Static_assert(
    (BOOT_SLOT_A_BASE_ADDRESS + BOOT_SLOT_A_REGION_SIZE) ==
        (BOOT_FLASH_BASE_ADDRESS + BOOT_FLASH_BANK_SIZE),
    "slot A must end at the bank 1 boundary");
_Static_assert(
    (BOOT_SLOT_B_BASE_ADDRESS + BOOT_SLOT_B_REGION_SIZE) ==
        BOOT_CONTROL_BASE_ADDRESS,
    "slot B and boot-control storage must be contiguous");
_Static_assert(
    (BOOT_CONTROL_BASE_ADDRESS + BOOT_CONTROL_REGION_SIZE) ==
        BOOT_FLASH_END_ADDRESS,
    "boot-control storage must end at the flash boundary");
_Static_assert(
    (BOOT_IMAGE_OFFSET_SIGNATURE + BOOT_IMAGE_SIGNATURE_SIZE) ==
        BOOT_IMAGE_MANIFEST_SIZE,
    "boot image manifest wire layout must remain 128 bytes");

static uint16_t Boot_Image_ReadU16Le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] |
                      ((uint16_t)data[1] << 8U));
}

static uint32_t Boot_Image_ReadU32Le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static uint8_t Boot_Image_IsRangeValid(uint32_t start,
                                       uint32_t length,
                                       uint32_t region_start,
                                       uint32_t region_size)
{
    uint32_t offset;

    if ((length == 0U) || (start < region_start))
    {
        return 0U;
    }

    offset = start - region_start;
    return ((offset <= region_size) &&
            (length <= (region_size - offset))) ? 1U : 0U;
}

static uint8_t Boot_Image_IsStackPointerValid(
    uint32_t stack_pointer,
    const Boot_ImageVerifier_t *verifier)
{
    uint32_t region_index;

    if ((stack_pointer & 0x7U) != 0U)
    {
        return 0U;
    }

    for (region_index = 0U;
         region_index < verifier->ram_region_count;
         region_index++)
    {
        const Boot_MemoryRegion_t *region =
            &verifier->ram_regions[region_index];

        if ((region->start_address < region->end_address) &&
            (stack_pointer > region->start_address) &&
            (stack_pointer <= region->end_address))
        {
            return 1U;
        }
    }

    return 0U;
}

Boot_ImageResult_t Boot_Image_Validate(
    const Boot_ImageSlot_t *slot,
    const Boot_ImageVerifier_t *verifier,
    Boot_ImageInfo_t *image_info)
{
    uint8_t manifest[BOOT_IMAGE_MANIFEST_SIZE];
    uint8_t vectors[8];
    uint32_t image_size;
    uint32_t image_address;
    uint32_t vector_address;
    uint32_t entry_address;
    uint32_t initial_stack_pointer;
    uint32_t reset_handler;
    uint32_t security_counter;
    uint16_t header_size;

    if (image_info != NULL)
    {
        *image_info = (Boot_ImageInfo_t){0};
    }

    if ((slot == NULL) || (verifier == NULL) ||
        (image_info == NULL) || (verifier->read == NULL) ||
        (verifier->verify_digest == NULL) ||
        (verifier->verify_signature == NULL) ||
        (verifier->ram_regions == NULL) ||
        (verifier->ram_region_count == 0U) ||
        (slot->region_size < BOOT_IMAGE_HEADER_SIZE))
    {
        return BOOT_IMAGE_RESULT_INVALID_ARGUMENT;
    }

    if (verifier->read(verifier->context,
                       slot->base_address,
                       manifest,
                       sizeof(manifest)) == 0U)
    {
        return BOOT_IMAGE_RESULT_READ_FAILED;
    }

    if (Boot_Image_ReadU32Le(&manifest[BOOT_IMAGE_OFFSET_MAGIC]) !=
        BOOT_IMAGE_MAGIC)
    {
        return BOOT_IMAGE_RESULT_BAD_MAGIC;
    }

    if (Boot_Image_ReadU16Le(
            &manifest[BOOT_IMAGE_OFFSET_FORMAT_VERSION]) !=
        BOOT_IMAGE_FORMAT_VERSION)
    {
        return BOOT_IMAGE_RESULT_BAD_FORMAT;
    }

    header_size = Boot_Image_ReadU16Le(
        &manifest[BOOT_IMAGE_OFFSET_HEADER_SIZE]);
    if ((header_size != BOOT_IMAGE_HEADER_SIZE) ||
        ((header_size % BOOT_IMAGE_VECTOR_ALIGNMENT) != 0U))
    {
        return BOOT_IMAGE_RESULT_BAD_HEADER;
    }

    if (Boot_Image_ReadU32Le(&manifest[BOOT_IMAGE_OFFSET_FLAGS]) != 0U)
    {
        return BOOT_IMAGE_RESULT_BAD_FLAGS;
    }

    image_size = Boot_Image_ReadU32Le(
        &manifest[BOOT_IMAGE_OFFSET_IMAGE_SIZE]);
    image_address = slot->base_address + (uint32_t)header_size;
    if ((image_address < slot->base_address) ||
        (Boot_Image_IsRangeValid(image_address,
                                 image_size,
                                 slot->base_address,
                                 slot->region_size) == 0U) ||
        (image_size < sizeof(vectors)))
    {
        return BOOT_IMAGE_RESULT_SIZE_OUT_OF_RANGE;
    }

    vector_address = Boot_Image_ReadU32Le(
        &manifest[BOOT_IMAGE_OFFSET_VECTOR_ADDRESS]);
    if (vector_address != image_address)
    {
        return BOOT_IMAGE_RESULT_VECTOR_ADDRESS;
    }

    security_counter = Boot_Image_ReadU32Le(
        &manifest[BOOT_IMAGE_OFFSET_SECURITY_COUNT]);
    if (security_counter < verifier->minimum_security_counter)
    {
        return BOOT_IMAGE_RESULT_SECURITY_ROLLBACK;
    }

    if (verifier->read(verifier->context,
                       vector_address,
                       vectors,
                       sizeof(vectors)) == 0U)
    {
        return BOOT_IMAGE_RESULT_READ_FAILED;
    }

    initial_stack_pointer = Boot_Image_ReadU32Le(&vectors[0]);
    if (Boot_Image_IsStackPointerValid(initial_stack_pointer,
                                       verifier) == 0U)
    {
        return BOOT_IMAGE_RESULT_STACK_POINTER;
    }

    entry_address = Boot_Image_ReadU32Le(
        &manifest[BOOT_IMAGE_OFFSET_ENTRY_ADDRESS]);
    reset_handler = Boot_Image_ReadU32Le(&vectors[4]);
    if (((entry_address & 1U) == 0U) ||
        (entry_address != reset_handler) ||
        (Boot_Image_IsRangeValid(entry_address & ~(uint32_t)1U,
                                 2U,
                                 image_address,
                                 image_size) == 0U))
    {
        return BOOT_IMAGE_RESULT_ENTRY_ADDRESS;
    }

    if (verifier->verify_signature(
            verifier->context,
            manifest,
            BOOT_IMAGE_SIGNED_REGION_SIZE,
            &manifest[BOOT_IMAGE_OFFSET_SIGNATURE]) == 0U)
    {
        return BOOT_IMAGE_RESULT_SIGNATURE_INVALID;
    }

    if (verifier->verify_digest(
            verifier->context,
            image_address,
            image_size,
            &manifest[BOOT_IMAGE_OFFSET_DIGEST]) == 0U)
    {
        return BOOT_IMAGE_RESULT_DIGEST_INVALID;
    }

    image_info->image_address = image_address;
    image_info->image_size = image_size;
    image_info->vector_address = vector_address;
    image_info->entry_address = entry_address;
    image_info->initial_stack_pointer = initial_stack_pointer;
    image_info->security_counter = security_counter;
    image_info->build_version = Boot_Image_ReadU32Le(
        &manifest[BOOT_IMAGE_OFFSET_BUILD_VERSION]);
    return BOOT_IMAGE_RESULT_OK;
}
