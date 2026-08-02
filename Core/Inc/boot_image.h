#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "boot_memory_map.h"

#define BOOT_IMAGE_MAGIC                 0x31474D49UL
#define BOOT_IMAGE_FORMAT_VERSION        1U
#define BOOT_IMAGE_DIGEST_SIZE           32U
#define BOOT_IMAGE_SIGNATURE_SIZE        64U
#define BOOT_IMAGE_SIGNED_REGION_SIZE    64U

#define BOOT_IMAGE_OFFSET_MAGIC          0U
#define BOOT_IMAGE_OFFSET_FORMAT_VERSION 4U
#define BOOT_IMAGE_OFFSET_HEADER_SIZE    6U
#define BOOT_IMAGE_OFFSET_IMAGE_SIZE     8U
#define BOOT_IMAGE_OFFSET_VECTOR_ADDRESS 12U
#define BOOT_IMAGE_OFFSET_ENTRY_ADDRESS  16U
#define BOOT_IMAGE_OFFSET_SECURITY_COUNT 20U
#define BOOT_IMAGE_OFFSET_BUILD_VERSION  24U
#define BOOT_IMAGE_OFFSET_FLAGS          28U
#define BOOT_IMAGE_OFFSET_DIGEST         32U
#define BOOT_IMAGE_OFFSET_SIGNATURE      64U

typedef enum
{
    BOOT_IMAGE_RESULT_OK = 0U,
    BOOT_IMAGE_RESULT_INVALID_ARGUMENT,
    BOOT_IMAGE_RESULT_READ_FAILED,
    BOOT_IMAGE_RESULT_BAD_MAGIC,
    BOOT_IMAGE_RESULT_BAD_FORMAT,
    BOOT_IMAGE_RESULT_BAD_HEADER,
    BOOT_IMAGE_RESULT_BAD_FLAGS,
    BOOT_IMAGE_RESULT_SIZE_OUT_OF_RANGE,
    BOOT_IMAGE_RESULT_VECTOR_ADDRESS,
    BOOT_IMAGE_RESULT_STACK_POINTER,
    BOOT_IMAGE_RESULT_ENTRY_ADDRESS,
    BOOT_IMAGE_RESULT_SECURITY_ROLLBACK,
    BOOT_IMAGE_RESULT_SIGNATURE_INVALID,
    BOOT_IMAGE_RESULT_DIGEST_INVALID
} Boot_ImageResult_t;

typedef struct
{
    uint32_t base_address;
    uint32_t region_size;
} Boot_ImageSlot_t;

typedef struct
{
    uint32_t start_address;
    uint32_t end_address;
} Boot_MemoryRegion_t;

typedef uint8_t (*Boot_ImageReadFn)(
    void *context,
    uint32_t address,
    uint8_t *data,
    uint32_t length
);

typedef uint8_t (*Boot_ImageDigestVerifyFn)(
    void *context,
    uint32_t image_address,
    uint32_t image_size,
    const uint8_t expected_digest[BOOT_IMAGE_DIGEST_SIZE]
);

typedef uint8_t (*Boot_ImageSignatureVerifyFn)(
    void *context,
    const uint8_t *signed_data,
    uint32_t signed_length,
    const uint8_t signature[BOOT_IMAGE_SIGNATURE_SIZE]
);

typedef struct
{
    Boot_ImageReadFn read;
    Boot_ImageDigestVerifyFn verify_digest;
    Boot_ImageSignatureVerifyFn verify_signature;
    void *context;
    const Boot_MemoryRegion_t *ram_regions;
    uint32_t ram_region_count;
    uint32_t minimum_security_counter;
} Boot_ImageVerifier_t;

typedef struct
{
    uint32_t image_address;
    uint32_t image_size;
    uint32_t vector_address;
    uint32_t entry_address;
    uint32_t initial_stack_pointer;
    uint32_t security_counter;
    uint32_t build_version;
} Boot_ImageInfo_t;

Boot_ImageResult_t Boot_Image_Validate(
    const Boot_ImageSlot_t *slot,
    const Boot_ImageVerifier_t *verifier,
    Boot_ImageInfo_t *image_info
);

#endif /* BOOT_IMAGE_H */
