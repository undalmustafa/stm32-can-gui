#ifndef UDS_DOWNLOAD_H
#define UDS_DOWNLOAD_H

/*
 * CALL_CONTEXT_DEFAULT: MAIN_LOOP_ONLY
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: none
 */

#include <stdint.h>

#include "boot_control.h"
#include "boot_memory_map.h"
#include "uds_server.h"

#define UDS_DOWNLOAD_ROUTINE_STATUS_READY      0U
#define UDS_DOWNLOAD_ROUTINE_STATUS_IN_PROGRESS 1U
#define UDS_DOWNLOAD_ROUTINE_STATUS_FAILED     2U

typedef enum
{
    UDS_DOWNLOAD_BACKEND_OK = 0,
    UDS_DOWNLOAD_BACKEND_PENDING,
    UDS_DOWNLOAD_BACKEND_FAILED
} Uds_DownloadBackendResult_t;

typedef enum
{
    UDS_DOWNLOAD_STATE_IDLE = 0,
    UDS_DOWNLOAD_STATE_ERASING,
    UDS_DOWNLOAD_STATE_READY,
    UDS_DOWNLOAD_STATE_TRANSFER,
    UDS_DOWNLOAD_STATE_COMPLETE,
    UDS_DOWNLOAD_STATE_FAILED
} Uds_DownloadState_t;

typedef uint8_t (*Uds_DownloadAuthorizeFn)(void *context);
typedef Uds_DownloadBackendResult_t (*Uds_DownloadEraseFn)(
    void *context,
    Boot_Slot_t target_slot,
    uint32_t slot_address,
    uint32_t slot_size);
typedef uint8_t (*Uds_DownloadWriteFn)(
    void *context,
    uint32_t address,
    const uint8_t *data,
    uint16_t length);
typedef uint8_t (*Uds_DownloadFinalizeFn)(
    void *context,
    Boot_Slot_t target_slot,
    uint32_t slot_address,
    const uint8_t header[BOOT_IMAGE_HEADER_SIZE],
    uint32_t artifact_size);
typedef void (*Uds_DownloadAbortFn)(void *context);

typedef struct
{
    Uds_DownloadAuthorizeFn authorize;
    Uds_DownloadEraseFn start_erase;
    Uds_DownloadEraseFn poll_erase;
    Uds_DownloadWriteFn write;
    Uds_DownloadFinalizeFn finalize;
    Uds_DownloadAbortFn abort;
    void *context;
    Boot_Slot_t running_slot;
} Uds_DownloadConfig_t;

typedef struct
{
    uint32_t erase_requests;
    uint32_t blocks_written;
    uint32_t duplicate_blocks;
    uint32_t bytes_received;
    uint32_t completed_downloads;
    uint32_t failed_downloads;
    Uds_DownloadState_t state;
    Boot_Slot_t target_slot;
    uint8_t expected_block_sequence;
} Uds_DownloadStats_t;

typedef struct
{
    Uds_DownloadConfig_t config;
    Uds_DownloadStats_t stats;
    uint8_t header[BOOT_IMAGE_HEADER_SIZE];
    uint32_t target_address;
    uint32_t target_size;
    uint32_t artifact_size;
    uint32_t received_size;
    uint8_t last_block_sequence;
    uint8_t has_last_block;
    uint8_t initialized;
} Uds_Download_t;

Uds_ServerResult_t Uds_Download_Init(
    Uds_Download_t *download,
    const Uds_DownloadConfig_t *config);

Uds_NegativeResponseCode_t Uds_Download_Handle(
    void *context,
    uint8_t service,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response_data,
    uint16_t response_capacity,
    uint16_t *response_data_length);

void Uds_Download_Abort(Uds_Download_t *download);
void Uds_Download_GetStats(
    const Uds_Download_t *download,
    Uds_DownloadStats_t *stats);

#endif /* UDS_DOWNLOAD_H */
