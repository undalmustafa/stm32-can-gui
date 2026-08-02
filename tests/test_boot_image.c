#include "unity.h"

#include "boot_image.h"

#include <stddef.h>
#include <string.h>

#define TEST_IMAGE_SIZE 1024U
#define TEST_STORAGE_SIZE (BOOT_IMAGE_HEADER_SIZE + TEST_IMAGE_SIZE)

typedef struct
{
    uint8_t storage[TEST_STORAGE_SIZE];
    uint32_t base_address;
    uint32_t read_count;
    uint32_t digest_count;
    uint32_t signature_count;
    uint8_t read_ok;
    uint8_t digest_ok;
    uint8_t signature_ok;
} Test_ImageContext_t;

static Test_ImageContext_t context;
static Boot_ImageSlot_t slot;
static Boot_ImageVerifier_t verifier;
static Boot_MemoryRegion_t ram_regions[2];

static void WriteU16Le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void WriteU32Le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint8_t ReadImage(void *callback_context,
                         uint32_t address,
                         uint8_t *data,
                         uint32_t length)
{
    Test_ImageContext_t *image_context =
        (Test_ImageContext_t *)callback_context;
    uint32_t offset;

    image_context->read_count++;
    if ((image_context->read_ok == 0U) ||
        (address < image_context->base_address))
    {
        return 0U;
    }

    offset = address - image_context->base_address;
    if ((offset > sizeof(image_context->storage)) ||
        (length > (sizeof(image_context->storage) - offset)))
    {
        return 0U;
    }

    (void)memcpy(data, &image_context->storage[offset], length);
    return 1U;
}

static uint8_t VerifyDigest(
    void *callback_context,
    uint32_t image_address,
    uint32_t image_size,
    const uint8_t expected_digest[BOOT_IMAGE_DIGEST_SIZE])
{
    Test_ImageContext_t *image_context =
        (Test_ImageContext_t *)callback_context;

    image_context->digest_count++;
    TEST_ASSERT_EQUAL_HEX32(
        image_context->base_address + BOOT_IMAGE_HEADER_SIZE,
        image_address);
    TEST_ASSERT_EQUAL_UINT32(TEST_IMAGE_SIZE, image_size);
    TEST_ASSERT_EQUAL_HEX8(0xA0U, expected_digest[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBFU,
                           expected_digest[BOOT_IMAGE_DIGEST_SIZE - 1U]);
    return image_context->digest_ok;
}

static uint8_t VerifySignature(
    void *callback_context,
    const uint8_t *signed_data,
    uint32_t signed_length,
    const uint8_t signature[BOOT_IMAGE_SIGNATURE_SIZE])
{
    Test_ImageContext_t *image_context =
        (Test_ImageContext_t *)callback_context;

    image_context->signature_count++;
    TEST_ASSERT_EQUAL_UINT32(BOOT_IMAGE_SIGNED_REGION_SIZE, signed_length);
    TEST_ASSERT_EQUAL_HEX32(
        BOOT_IMAGE_MAGIC,
        (uint32_t)signed_data[0] |
        ((uint32_t)signed_data[1] << 8U) |
        ((uint32_t)signed_data[2] << 16U) |
        ((uint32_t)signed_data[3] << 24U));
    TEST_ASSERT_EQUAL_HEX8(0xC0U, signature[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU,
                           signature[BOOT_IMAGE_SIGNATURE_SIZE - 1U]);
    return image_context->signature_ok;
}

static void BuildValidImage(void)
{
    uint32_t index;
    uint32_t image_address = slot.base_address + BOOT_IMAGE_HEADER_SIZE;
    uint32_t entry_address = image_address + 0x101U;

    (void)memset(context.storage, 0, sizeof(context.storage));
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_MAGIC], BOOT_IMAGE_MAGIC);
    WriteU16Le(&context.storage[BOOT_IMAGE_OFFSET_FORMAT_VERSION],
               BOOT_IMAGE_FORMAT_VERSION);
    WriteU16Le(&context.storage[BOOT_IMAGE_OFFSET_HEADER_SIZE],
               BOOT_IMAGE_HEADER_SIZE);
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_IMAGE_SIZE],
               TEST_IMAGE_SIZE);
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_VECTOR_ADDRESS],
               image_address);
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_ENTRY_ADDRESS],
               entry_address);
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_SECURITY_COUNT], 7U);
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_BUILD_VERSION], 0x01020304U);
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_FLAGS], 0U);

    for (index = 0U; index < BOOT_IMAGE_DIGEST_SIZE; index++)
    {
        context.storage[BOOT_IMAGE_OFFSET_DIGEST + index] =
            (uint8_t)(0xA0U + index);
    }
    for (index = 0U; index < BOOT_IMAGE_SIGNATURE_SIZE; index++)
    {
        context.storage[BOOT_IMAGE_OFFSET_SIGNATURE + index] =
            (uint8_t)(0xC0U + index);
    }

    WriteU32Le(&context.storage[BOOT_IMAGE_HEADER_SIZE], 0x24001000UL);
    WriteU32Le(&context.storage[BOOT_IMAGE_HEADER_SIZE + 4U], entry_address);
}

void setUp(void)
{
    context = (Test_ImageContext_t){0};
    context.base_address = BOOT_SLOT_A_BASE_ADDRESS;
    context.read_ok = 1U;
    context.digest_ok = 1U;
    context.signature_ok = 1U;
    slot.base_address = BOOT_SLOT_A_BASE_ADDRESS;
    slot.region_size = BOOT_SLOT_A_REGION_SIZE;
    ram_regions[0] = (Boot_MemoryRegion_t){0x20000000UL, 0x20020000UL};
    ram_regions[1] = (Boot_MemoryRegion_t){0x24000000UL, 0x24100000UL};
    verifier.read = ReadImage;
    verifier.verify_digest = VerifyDigest;
    verifier.verify_signature = VerifySignature;
    verifier.context = &context;
    verifier.ram_regions = ram_regions;
    verifier.ram_region_count = 2U;
    verifier.minimum_security_counter = 5U;
    BuildValidImage();
}

void tearDown(void)
{
}

static void test_valid_signed_image_exposes_boot_evidence(void)
{
    Boot_ImageInfo_t image_info;

    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_OK,
        Boot_Image_Validate(&slot, &verifier, &image_info));
    TEST_ASSERT_EQUAL_HEX32(
        BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE,
        image_info.image_address);
    TEST_ASSERT_EQUAL_UINT32(TEST_IMAGE_SIZE, image_info.image_size);
    TEST_ASSERT_EQUAL_HEX32(0x24001000UL,
                            image_info.initial_stack_pointer);
    TEST_ASSERT_EQUAL_UINT32(7U, image_info.security_counter);
    TEST_ASSERT_EQUAL_HEX32(0x01020304UL, image_info.build_version);
    TEST_ASSERT_EQUAL_UINT32(1U, context.signature_count);
    TEST_ASSERT_EQUAL_UINT32(1U, context.digest_count);
}

static void test_manifest_identity_header_and_flags_fail_closed(void)
{
    Boot_ImageInfo_t image_info;

    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_MAGIC], 0U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_BAD_MAGIC,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU16Le(&context.storage[BOOT_IMAGE_OFFSET_FORMAT_VERSION], 2U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_BAD_FORMAT,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU16Le(&context.storage[BOOT_IMAGE_OFFSET_HEADER_SIZE], 128U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_BAD_HEADER,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_FLAGS], 1U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_BAD_FLAGS,
        Boot_Image_Validate(&slot, &verifier, &image_info));
}

static void test_image_bounds_and_vector_location_are_enforced(void)
{
    Boot_ImageInfo_t image_info;

    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_IMAGE_SIZE],
               BOOT_SLOT_A_REGION_SIZE);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_SIZE_OUT_OF_RANGE,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_VECTOR_ADDRESS],
               BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE + 4U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_VECTOR_ADDRESS,
        Boot_Image_Validate(&slot, &verifier, &image_info));
}

static void test_security_counter_rejects_rollback_before_crypto(void)
{
    Boot_ImageInfo_t image_info;

    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_SECURITY_COUNT], 4U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_SECURITY_ROLLBACK,
        Boot_Image_Validate(&slot, &verifier, &image_info));
    TEST_ASSERT_EQUAL_UINT32(0U, context.signature_count);
    TEST_ASSERT_EQUAL_UINT32(0U, context.digest_count);
}

static void test_stack_and_reset_vectors_must_be_bootable(void)
{
    Boot_ImageInfo_t image_info;

    WriteU32Le(&context.storage[BOOT_IMAGE_HEADER_SIZE], 0x24001004UL);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_STACK_POINTER,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU32Le(&context.storage[BOOT_IMAGE_HEADER_SIZE], 0x30000000UL);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_STACK_POINTER,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU32Le(&context.storage[BOOT_IMAGE_HEADER_SIZE + 4U],
               BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE + 0x201U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_ENTRY_ADDRESS,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    BuildValidImage();
    WriteU32Le(&context.storage[BOOT_IMAGE_OFFSET_ENTRY_ADDRESS],
               BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE + 0x100U);
    WriteU32Le(&context.storage[BOOT_IMAGE_HEADER_SIZE + 4U],
               BOOT_SLOT_A_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE + 0x100U);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_ENTRY_ADDRESS,
        Boot_Image_Validate(&slot, &verifier, &image_info));
}

static void test_signature_digest_and_storage_failures_are_distinct(void)
{
    Boot_ImageInfo_t image_info;

    context.signature_ok = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_SIGNATURE_INVALID,
        Boot_Image_Validate(&slot, &verifier, &image_info));
    TEST_ASSERT_EQUAL_UINT32(0U, context.digest_count);

    context.signature_ok = 1U;
    context.digest_ok = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_DIGEST_INVALID,
        Boot_Image_Validate(&slot, &verifier, &image_info));

    context.read_ok = 0U;
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_READ_FAILED,
        Boot_Image_Validate(&slot, &verifier, &image_info));
}

static void test_memory_map_is_sector_aligned_and_non_overlapping(void)
{
    TEST_ASSERT_EQUAL_HEX32(
        BOOT_FLASH_BASE_ADDRESS + BOOT_FLASH_BANK_SIZE,
        BOOT_SLOT_A_BASE_ADDRESS + BOOT_SLOT_A_REGION_SIZE);
    TEST_ASSERT_EQUAL_HEX32(
        BOOT_FLASH_END_ADDRESS,
        BOOT_CONTROL_BASE_ADDRESS + BOOT_CONTROL_REGION_SIZE);
    TEST_ASSERT_EQUAL_UINT32(
        0U, BOOT_LOADER_REGION_SIZE % BOOT_FLASH_SECTOR_SIZE);
    TEST_ASSERT_EQUAL_UINT32(
        0U, BOOT_SLOT_A_REGION_SIZE % BOOT_FLASH_SECTOR_SIZE);
    TEST_ASSERT_EQUAL_UINT32(
        0U, BOOT_SLOT_B_REGION_SIZE % BOOT_FLASH_SECTOR_SIZE);
    TEST_ASSERT_EQUAL_UINT32(
        0U, BOOT_CONTROL_REGION_SIZE % BOOT_FLASH_SECTOR_SIZE);
}

static void test_null_dependencies_are_rejected_and_output_is_cleared(void)
{
    Boot_ImageInfo_t image_info = {
        .image_address = 1U,
        .image_size = 2U,
        .vector_address = 3U,
        .entry_address = 4U,
        .initial_stack_pointer = 5U,
        .security_counter = 6U,
        .build_version = 7U,
    };

    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_INVALID_ARGUMENT,
        Boot_Image_Validate(NULL, &verifier, &image_info));
    TEST_ASSERT_EQUAL_UINT32(0U, image_info.image_address);
    TEST_ASSERT_EQUAL(
        BOOT_IMAGE_RESULT_INVALID_ARGUMENT,
        Boot_Image_Validate(&slot, &verifier, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_signed_image_exposes_boot_evidence);
    RUN_TEST(test_manifest_identity_header_and_flags_fail_closed);
    RUN_TEST(test_image_bounds_and_vector_location_are_enforced);
    RUN_TEST(test_security_counter_rejects_rollback_before_crypto);
    RUN_TEST(test_stack_and_reset_vectors_must_be_bootable);
    RUN_TEST(test_signature_digest_and_storage_failures_are_distinct);
    RUN_TEST(test_memory_map_is_sector_aligned_and_non_overlapping);
    RUN_TEST(test_null_dependencies_are_rejected_and_output_is_cleared);
    return UNITY_END();
}
