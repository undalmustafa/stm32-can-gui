#include "uds_download.h"

#include <stddef.h>
#include <string.h>

#define UDS_ROUTINE_START             0x01U
#define UDS_ROUTINE_REQUEST_RESULTS   0x03U
#define UDS_REQUEST_DOWNLOAD_LENGTH   11U
#define UDS_ROUTINE_REQUEST_LENGTH    4U
#define UDS_ROUTINE_RESPONSE_LENGTH   13U

static uint32_t Uds_Download_ReadU32Be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static void Uds_Download_WriteU32Be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void Uds_Download_SetState(
    Uds_Download_t *download,
    Uds_DownloadState_t state)
{
    download->stats.state = state;
}

static void Uds_Download_Fail(Uds_Download_t *download)
{
    Uds_Download_SetState(download, UDS_DOWNLOAD_STATE_FAILED);
    download->stats.failed_downloads++;
    if (download->config.abort != NULL)
    {
        download->config.abort(download->config.context);
    }
}

static uint32_t Uds_Download_SlotAddress(Boot_Slot_t slot)
{
    return (slot == BOOT_SLOT_A)
        ? BOOT_SLOT_A_BASE_ADDRESS
        : BOOT_SLOT_B_BASE_ADDRESS;
}

static uint32_t Uds_Download_SlotSize(Boot_Slot_t slot)
{
    return (slot == BOOT_SLOT_A)
        ? BOOT_SLOT_A_REGION_SIZE
        : BOOT_SLOT_B_REGION_SIZE;
}

static Uds_NegativeResponseCode_t Uds_Download_WriteRoutineResponse(
    Uds_Download_t *download,
    uint8_t subfunction,
    uint8_t routine_status,
    uint8_t *response_data,
    uint16_t response_capacity,
    uint16_t *response_data_length)
{
    if (response_capacity < UDS_ROUTINE_RESPONSE_LENGTH)
    {
        return UDS_NRC_RESPONSE_TOO_LONG;
    }

    response_data[0] = subfunction;
    response_data[1] = (uint8_t)(
        CAN_PROTOCOL_UDS_ROUTINE_ERASE_INACTIVE_SLOT >> 8U);
    response_data[2] = (uint8_t)
        CAN_PROTOCOL_UDS_ROUTINE_ERASE_INACTIVE_SLOT;
    response_data[3] = routine_status;
    response_data[4] = (uint8_t)download->stats.target_slot;
    Uds_Download_WriteU32Be(&response_data[5], download->target_address);
    Uds_Download_WriteU32Be(&response_data[9], download->target_size);
    *response_data_length = UDS_ROUTINE_RESPONSE_LENGTH;
    return UDS_NRC_NONE;
}

static Uds_NegativeResponseCode_t Uds_Download_ProcessRoutine(
    Uds_Download_t *download,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response_data,
    uint16_t response_capacity,
    uint16_t *response_data_length)
{
    Uds_DownloadBackendResult_t backend_result;
    uint8_t routine_status;

    if (request_length != UDS_ROUTINE_REQUEST_LENGTH)
    {
        return UDS_NRC_INCORRECT_MESSAGE_LENGTH;
    }
    if ((((uint16_t)request[2] << 8U) | request[3]) !=
        CAN_PROTOCOL_UDS_ROUTINE_ERASE_INACTIVE_SLOT)
    {
        return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }

    if (request[1] == UDS_ROUTINE_START)
    {
        if (download->config.authorize(
                download->config.context) == 0U)
        {
            return UDS_NRC_SECURITY_ACCESS_DENIED;
        }
        if (download->stats.state != UDS_DOWNLOAD_STATE_IDLE)
        {
            return UDS_NRC_REQUEST_SEQUENCE_ERROR;
        }
        download->stats.target_slot =
            (download->config.running_slot == BOOT_SLOT_A)
                ? BOOT_SLOT_B : BOOT_SLOT_A;
        download->target_address = Uds_Download_SlotAddress(
            download->stats.target_slot);
        download->target_size = Uds_Download_SlotSize(
            download->stats.target_slot);
        download->stats.erase_requests++;
        backend_result = download->config.start_erase(
            download->config.context,
            download->stats.target_slot,
            download->target_address,
            download->target_size);
    }
    else if (request[1] == UDS_ROUTINE_REQUEST_RESULTS)
    {
        if (download->stats.state == UDS_DOWNLOAD_STATE_ERASING)
        {
            backend_result = download->config.poll_erase(
                download->config.context,
                download->stats.target_slot,
                download->target_address,
                download->target_size);
        }
        else if (download->stats.state == UDS_DOWNLOAD_STATE_READY)
        {
            backend_result = UDS_DOWNLOAD_BACKEND_OK;
        }
        else if (download->stats.state == UDS_DOWNLOAD_STATE_FAILED)
        {
            backend_result = UDS_DOWNLOAD_BACKEND_FAILED;
        }
        else
        {
            return UDS_NRC_REQUEST_SEQUENCE_ERROR;
        }
    }
    else
    {
        return UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    }

    if (backend_result == UDS_DOWNLOAD_BACKEND_OK)
    {
        Uds_Download_SetState(download, UDS_DOWNLOAD_STATE_READY);
        routine_status = UDS_DOWNLOAD_ROUTINE_STATUS_READY;
    }
    else if (backend_result == UDS_DOWNLOAD_BACKEND_PENDING)
    {
        Uds_Download_SetState(download, UDS_DOWNLOAD_STATE_ERASING);
        routine_status = UDS_DOWNLOAD_ROUTINE_STATUS_IN_PROGRESS;
    }
    else
    {
        Uds_Download_Fail(download);
        routine_status = UDS_DOWNLOAD_ROUTINE_STATUS_FAILED;
    }

    return Uds_Download_WriteRoutineResponse(
        download,
        request[1],
        routine_status,
        response_data,
        response_capacity,
        response_data_length);
}

static Uds_NegativeResponseCode_t Uds_Download_ProcessRequestDownload(
    Uds_Download_t *download,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response_data,
    uint16_t response_capacity,
    uint16_t *response_data_length)
{
    uint32_t address;
    uint32_t size;

    if (request_length != UDS_REQUEST_DOWNLOAD_LENGTH)
    {
        return UDS_NRC_INCORRECT_MESSAGE_LENGTH;
    }
    if ((request[1] != CAN_PROTOCOL_UDS_DOWNLOAD_DATA_FORMAT_IDENTIFIER) ||
        (request[2] !=
         CAN_PROTOCOL_UDS_DOWNLOAD_ADDRESS_LENGTH_FORMAT_IDENTIFIER))
    {
        return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    if (download->stats.state != UDS_DOWNLOAD_STATE_READY)
    {
        return UDS_NRC_REQUEST_SEQUENCE_ERROR;
    }

    address = Uds_Download_ReadU32Be(&request[3]);
    size = Uds_Download_ReadU32Be(&request[7]);
    if ((address != download->target_address) ||
        (size < BOOT_IMAGE_HEADER_SIZE) ||
        (size > download->target_size) ||
        ((size % BOOT_FLASH_PROGRAM_UNIT_SIZE) != 0U))
    {
        return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    if (response_capacity < 3U)
    {
        return UDS_NRC_RESPONSE_TOO_LONG;
    }

    (void)memset(download->header, 0xFF, sizeof(download->header));
    download->artifact_size = size;
    download->received_size = 0U;
    download->stats.expected_block_sequence = 1U;
    download->has_last_block = 0U;
    Uds_Download_SetState(download, UDS_DOWNLOAD_STATE_TRANSFER);
    response_data[0] = 0x20U;
    response_data[1] = (uint8_t)(
        CAN_PROTOCOL_UDS_DOWNLOAD_MAX_BLOCK_LENGTH >> 8U);
    response_data[2] = (uint8_t)
        CAN_PROTOCOL_UDS_DOWNLOAD_MAX_BLOCK_LENGTH;
    *response_data_length = 3U;
    return UDS_NRC_NONE;
}

static Uds_NegativeResponseCode_t Uds_Download_ProcessTransferData(
    Uds_Download_t *download,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response_data,
    uint16_t response_capacity,
    uint16_t *response_data_length)
{
    const uint8_t *block_data;
    uint16_t block_length;
    uint16_t header_length = 0U;

    if ((request_length < 3U) ||
        (request_length > CAN_PROTOCOL_UDS_DOWNLOAD_MAX_BLOCK_LENGTH))
    {
        return UDS_NRC_INCORRECT_MESSAGE_LENGTH;
    }
    if (download->stats.state != UDS_DOWNLOAD_STATE_TRANSFER)
    {
        return UDS_NRC_REQUEST_SEQUENCE_ERROR;
    }
    if ((download->has_last_block != 0U) &&
        (request[1] == download->last_block_sequence))
    {
        if (response_capacity < 1U)
        {
            return UDS_NRC_RESPONSE_TOO_LONG;
        }
        download->stats.duplicate_blocks++;
        response_data[0] = request[1];
        *response_data_length = 1U;
        return UDS_NRC_NONE;
    }
    if (request[1] != download->stats.expected_block_sequence)
    {
        return UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER;
    }

    block_data = &request[2];
    block_length = (uint16_t)(request_length - 2U);
    if ((uint32_t)block_length >
        (download->artifact_size - download->received_size))
    {
        Uds_Download_Fail(download);
        return UDS_NRC_TRANSFER_DATA_SUSPENDED;
    }
    if (response_capacity < 1U)
    {
        return UDS_NRC_RESPONSE_TOO_LONG;
    }

    if (download->received_size < BOOT_IMAGE_HEADER_SIZE)
    {
        uint32_t header_remaining =
            BOOT_IMAGE_HEADER_SIZE - download->received_size;
        header_length = (uint16_t)(
            ((uint32_t)block_length < header_remaining)
                ? block_length : header_remaining);
        (void)memcpy(
            &download->header[download->received_size],
            block_data,
            header_length);
    }
    if (header_length < block_length)
    {
        uint16_t write_length = (uint16_t)(block_length - header_length);
        uint32_t write_address = download->target_address +
            download->received_size + header_length;

        if (download->config.write(
                download->config.context,
                write_address,
                &block_data[header_length],
                write_length) == 0U)
        {
            Uds_Download_Fail(download);
            return UDS_NRC_GENERAL_PROGRAMMING_FAILURE;
        }
    }

    download->received_size += block_length;
    download->stats.bytes_received += block_length;
    download->stats.blocks_written++;
    download->last_block_sequence = request[1];
    download->has_last_block = 1U;
    download->stats.expected_block_sequence =
        (uint8_t)(download->stats.expected_block_sequence + 1U);
    response_data[0] = request[1];
    *response_data_length = 1U;
    return UDS_NRC_NONE;
}

static Uds_NegativeResponseCode_t Uds_Download_ProcessTransferExit(
    Uds_Download_t *download,
    uint16_t request_length,
    uint16_t *response_data_length)
{
    if (request_length != 1U)
    {
        return UDS_NRC_INCORRECT_MESSAGE_LENGTH;
    }
    if ((download->stats.state != UDS_DOWNLOAD_STATE_TRANSFER) ||
        (download->received_size != download->artifact_size))
    {
        return UDS_NRC_REQUEST_SEQUENCE_ERROR;
    }

    if (download->config.finalize(
            download->config.context,
            download->stats.target_slot,
            download->target_address,
            download->header,
            download->artifact_size) == 0U)
    {
        Uds_Download_Fail(download);
        return UDS_NRC_GENERAL_PROGRAMMING_FAILURE;
    }

    Uds_Download_SetState(download, UDS_DOWNLOAD_STATE_COMPLETE);
    download->stats.completed_downloads++;
    *response_data_length = 0U;
    return UDS_NRC_NONE;
}

Uds_ServerResult_t Uds_Download_Init(
    Uds_Download_t *download,
    const Uds_DownloadConfig_t *config)
{
    if ((download == NULL) || (config == NULL) ||
        (config->authorize == NULL) ||
        (config->start_erase == NULL) ||
        (config->poll_erase == NULL) ||
        (config->write == NULL) || (config->finalize == NULL) ||
        ((config->running_slot != BOOT_SLOT_A) &&
         (config->running_slot != BOOT_SLOT_B)))
    {
        return UDS_SERVER_RESULT_INVALID_ARGUMENT;
    }

    *download = (Uds_Download_t){0};
    download->config = *config;
    download->stats.state = UDS_DOWNLOAD_STATE_IDLE;
    download->initialized = 1U;
    return UDS_SERVER_RESULT_OK;
}

Uds_NegativeResponseCode_t Uds_Download_Handle(
    void *context,
    uint8_t service,
    const uint8_t *request,
    uint16_t request_length,
    uint8_t *response_data,
    uint16_t response_capacity,
    uint16_t *response_data_length)
{
    Uds_Download_t *download = (Uds_Download_t *)context;

    if ((download == NULL) || (request == NULL) ||
        (response_data == NULL) || (response_data_length == NULL) ||
        (request_length == 0U) || (download->initialized == 0U))
    {
        return UDS_NRC_CONDITIONS_NOT_CORRECT;
    }
    *response_data_length = 0U;

    switch (service)
    {
        case CAN_PROTOCOL_UDS_SERVICE_ROUTINE_CONTROL:
            return Uds_Download_ProcessRoutine(
                download, request, request_length,
                response_data, response_capacity, response_data_length);

        case CAN_PROTOCOL_UDS_SERVICE_REQUEST_DOWNLOAD:
            return Uds_Download_ProcessRequestDownload(
                download, request, request_length,
                response_data, response_capacity, response_data_length);

        case CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA:
            return Uds_Download_ProcessTransferData(
                download, request, request_length,
                response_data, response_capacity, response_data_length);

        case CAN_PROTOCOL_UDS_SERVICE_REQUEST_TRANSFER_EXIT:
            return Uds_Download_ProcessTransferExit(
                download, request_length, response_data_length);

        default:
            return UDS_NRC_SERVICE_NOT_SUPPORTED;
    }
}

void Uds_Download_Abort(Uds_Download_t *download)
{
    if ((download != NULL) && (download->initialized != 0U))
    {
        if ((download->config.abort != NULL) &&
            ((download->stats.state == UDS_DOWNLOAD_STATE_ERASING) ||
             (download->stats.state == UDS_DOWNLOAD_STATE_READY) ||
             (download->stats.state == UDS_DOWNLOAD_STATE_TRANSFER)))
        {
            download->config.abort(download->config.context);
        }
        download->stats.state = UDS_DOWNLOAD_STATE_IDLE;
        download->stats.target_slot = BOOT_SLOT_NONE;
        download->target_address = 0U;
        download->target_size = 0U;
        download->artifact_size = 0U;
        download->received_size = 0U;
        download->stats.expected_block_sequence = 0U;
        download->has_last_block = 0U;
    }
}

void Uds_Download_GetStats(
    const Uds_Download_t *download,
    Uds_DownloadStats_t *stats)
{
    if ((download != NULL) && (stats != NULL))
    {
        *stats = download->stats;
    }
}
