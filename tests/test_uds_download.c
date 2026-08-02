#include "unity.h"

#include "uds_download.h"

#include <string.h>

typedef struct
{
    uint8_t authorized;
    Uds_DownloadBackendResult_t erase_start_result;
    Uds_DownloadBackendResult_t erase_poll_result;
    uint8_t write_succeeds;
    uint8_t finalize_succeeds;
    uint32_t write_address;
    uint16_t write_length;
    uint8_t write_data[256U];
    uint8_t finalized_header[BOOT_IMAGE_HEADER_SIZE];
    uint32_t finalized_size;
    Boot_Slot_t finalized_slot;
    uint8_t erase_start_calls;
    uint8_t erase_poll_calls;
    uint8_t write_calls;
    uint8_t finalize_calls;
    uint8_t abort_calls;
} DownloadFixture_t;

static DownloadFixture_t fixture;
static Uds_Download_t download;
static uint8_t response[32U];
static uint16_t response_length;

static uint8_t Authorize(void *context)
{
    return ((DownloadFixture_t *)context)->authorized;
}

static Uds_DownloadBackendResult_t StartErase(
    void *context,
    Boot_Slot_t target_slot,
    uint32_t slot_address,
    uint32_t slot_size)
{
    DownloadFixture_t *state = (DownloadFixture_t *)context;

    state->erase_start_calls++;
    TEST_ASSERT_EQUAL(BOOT_SLOT_B, target_slot);
    TEST_ASSERT_EQUAL_HEX32(BOOT_SLOT_B_BASE_ADDRESS, slot_address);
    TEST_ASSERT_EQUAL_HEX32(BOOT_SLOT_B_REGION_SIZE, slot_size);
    return state->erase_start_result;
}

static Uds_DownloadBackendResult_t PollErase(
    void *context,
    Boot_Slot_t target_slot,
    uint32_t slot_address,
    uint32_t slot_size)
{
    DownloadFixture_t *state = (DownloadFixture_t *)context;

    state->erase_poll_calls++;
    TEST_ASSERT_EQUAL(BOOT_SLOT_B, target_slot);
    TEST_ASSERT_EQUAL_HEX32(BOOT_SLOT_B_BASE_ADDRESS, slot_address);
    TEST_ASSERT_EQUAL_HEX32(BOOT_SLOT_B_REGION_SIZE, slot_size);
    return state->erase_poll_result;
}

static uint8_t Write(void *context,
                     uint32_t address,
                     const uint8_t *data,
                     uint16_t length)
{
    DownloadFixture_t *state = (DownloadFixture_t *)context;

    state->write_calls++;
    state->write_address = address;
    state->write_length = length;
    TEST_ASSERT_TRUE(length <= sizeof(state->write_data));
    (void)memcpy(state->write_data, data, length);
    return state->write_succeeds;
}

static uint8_t Finalize(
    void *context,
    Boot_Slot_t target_slot,
    uint32_t slot_address,
    const uint8_t header[BOOT_IMAGE_HEADER_SIZE],
    uint32_t artifact_size)
{
    DownloadFixture_t *state = (DownloadFixture_t *)context;

    state->finalize_calls++;
    state->finalized_slot = target_slot;
    state->finalized_size = artifact_size;
    TEST_ASSERT_EQUAL_HEX32(BOOT_SLOT_B_BASE_ADDRESS, slot_address);
    (void)memcpy(state->finalized_header, header, BOOT_IMAGE_HEADER_SIZE);
    return state->finalize_succeeds;
}

static void Abort(void *context)
{
    ((DownloadFixture_t *)context)->abort_calls++;
}

static Uds_NegativeResponseCode_t Handle(
    const uint8_t *request,
    uint16_t request_length)
{
    return Uds_Download_Handle(
        &download,
        request[0],
        request,
        request_length,
        response,
        sizeof(response),
        &response_length);
}

static void WriteU32Be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void PrepareReady(void)
{
    const uint8_t erase[4U] = {
        CAN_PROTOCOL_UDS_SERVICE_ROUTINE_CONTROL,
        0x01U,
        (uint8_t)(CAN_PROTOCOL_UDS_ROUTINE_ERASE_INACTIVE_SLOT >> 8U),
        (uint8_t)CAN_PROTOCOL_UDS_ROUTINE_ERASE_INACTIVE_SLOT
    };
    uint8_t results[4U];

    TEST_ASSERT_EQUAL(UDS_NRC_NONE, Handle(erase, sizeof(erase)));
    TEST_ASSERT_EQUAL_UINT8(UDS_DOWNLOAD_ROUTINE_STATUS_IN_PROGRESS,
                            response[3]);
    (void)memcpy(results, erase, sizeof(results));
    results[1] = 0x03U;
    TEST_ASSERT_EQUAL(UDS_NRC_NONE, Handle(results, sizeof(results)));
    TEST_ASSERT_EQUAL_UINT8(UDS_DOWNLOAD_ROUTINE_STATUS_READY, response[3]);
}

static void RequestArtifact(uint32_t address, uint32_t size)
{
    uint8_t request[11U] = {
        CAN_PROTOCOL_UDS_SERVICE_REQUEST_DOWNLOAD,
        CAN_PROTOCOL_UDS_DOWNLOAD_DATA_FORMAT_IDENTIFIER,
        CAN_PROTOCOL_UDS_DOWNLOAD_ADDRESS_LENGTH_FORMAT_IDENTIFIER,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };

    WriteU32Be(&request[3], address);
    WriteU32Be(&request[7], size);
    TEST_ASSERT_EQUAL(UDS_NRC_NONE, Handle(request, sizeof(request)));
    TEST_ASSERT_EQUAL_UINT16(3U, response_length);
    TEST_ASSERT_EQUAL_HEX8(0x20U, response[0]);
    TEST_ASSERT_EQUAL_UINT16(
        CAN_PROTOCOL_UDS_DOWNLOAD_MAX_BLOCK_LENGTH,
        ((uint16_t)response[1] << 8U) | response[2]);
}

void setUp(void)
{
    Uds_DownloadConfig_t config = {0};

    (void)memset(&fixture, 0, sizeof(fixture));
    fixture.authorized = 1U;
    fixture.erase_start_result = UDS_DOWNLOAD_BACKEND_PENDING;
    fixture.erase_poll_result = UDS_DOWNLOAD_BACKEND_OK;
    fixture.write_succeeds = 1U;
    fixture.finalize_succeeds = 1U;
    config.authorize = Authorize;
    config.start_erase = StartErase;
    config.poll_erase = PollErase;
    config.write = Write;
    config.finalize = Finalize;
    config.abort = Abort;
    config.context = &fixture;
    config.running_slot = BOOT_SLOT_A;
    TEST_ASSERT_EQUAL(
        UDS_SERVER_RESULT_OK,
        Uds_Download_Init(&download, &config));
}

void tearDown(void)
{
}

static void test_erase_selects_only_inactive_slot_and_reports_progress(void)
{
    Uds_DownloadStats_t stats;

    PrepareReady();
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.erase_start_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.erase_poll_calls);
    TEST_ASSERT_EQUAL_UINT8(BOOT_SLOT_B, response[4]);
    TEST_ASSERT_EQUAL_HEX8(0x08U, response[5]);
    TEST_ASSERT_EQUAL_HEX8(0x10U, response[6]);
    Uds_Download_GetStats(&download, &stats);
    TEST_ASSERT_EQUAL(UDS_DOWNLOAD_STATE_READY, stats.state);
    TEST_ASSERT_EQUAL(BOOT_SLOT_B, stats.target_slot);
}

static void test_header_is_committed_only_after_payload_and_finalize(void)
{
    uint8_t transfer[258U];
    uint8_t exit_request[1U] = {
        CAN_PROTOCOL_UDS_SERVICE_REQUEST_TRANSFER_EXIT
    };
    uint16_t block;
    uint16_t index;

    PrepareReady();
    RequestArtifact(BOOT_SLOT_B_BASE_ADDRESS, 1040U);
    for (block = 1U; block <= 4U; block++)
    {
        transfer[0] = CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA;
        transfer[1] = (uint8_t)block;
        for (index = 0U; index < 256U; index++)
        {
            transfer[index + 2U] = (uint8_t)(index + block);
        }
        TEST_ASSERT_EQUAL(UDS_NRC_NONE,
                          Handle(transfer, sizeof(transfer)));
    }
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.write_calls);
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.finalize_calls);

    transfer[0] = CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA;
    transfer[1] = 5U;
    for (index = 0U; index < 16U; index++)
    {
        transfer[index + 2U] = (uint8_t)(0xA0U + index);
    }
    TEST_ASSERT_EQUAL(UDS_NRC_NONE, Handle(transfer, 18U));
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.write_calls);
    TEST_ASSERT_EQUAL_HEX32(
        BOOT_SLOT_B_BASE_ADDRESS + BOOT_IMAGE_HEADER_SIZE,
        fixture.write_address);
    TEST_ASSERT_EQUAL_UINT16(16U, fixture.write_length);
    TEST_ASSERT_EQUAL_HEX8(0xA0U, fixture.write_data[0]);

    TEST_ASSERT_EQUAL(UDS_NRC_NONE,
                      Handle(exit_request, sizeof(exit_request)));
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.finalize_calls);
    TEST_ASSERT_EQUAL(BOOT_SLOT_B, fixture.finalized_slot);
    TEST_ASSERT_EQUAL_UINT32(1040U, fixture.finalized_size);
    TEST_ASSERT_EQUAL_HEX8(1U, fixture.finalized_header[0]);
    TEST_ASSERT_EQUAL_UINT16(0U, response_length);
}

static void test_sequence_and_duplicate_blocks_do_not_double_program(void)
{
    uint8_t block[18U] = {
        CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA, 2U
    };
    uint8_t header_block[258U] = {
        CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA, 1U
    };

    PrepareReady();
    RequestArtifact(BOOT_SLOT_B_BASE_ADDRESS, 1040U);
    TEST_ASSERT_EQUAL(UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER,
                      Handle(block, sizeof(block)));
    TEST_ASSERT_EQUAL(UDS_NRC_NONE,
                      Handle(header_block, sizeof(header_block)));
    TEST_ASSERT_EQUAL(UDS_NRC_NONE,
                      Handle(header_block, sizeof(header_block)));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.write_calls);
    TEST_ASSERT_EQUAL_UINT32(256U, download.received_size);
    TEST_ASSERT_EQUAL_UINT32(1U, download.stats.duplicate_blocks);
}

static void test_access_range_and_backend_failures_are_fail_closed(void)
{
    uint8_t erase[4U] = {
        CAN_PROTOCOL_UDS_SERVICE_ROUTINE_CONTROL, 0x01U, 0xFFU, 0x00U
    };
    uint8_t block[258U] = {
        CAN_PROTOCOL_UDS_SERVICE_TRANSFER_DATA, 1U
    };

    fixture.authorized = 0U;
    TEST_ASSERT_EQUAL(UDS_NRC_SECURITY_ACCESS_DENIED,
                      Handle(erase, sizeof(erase)));
    TEST_ASSERT_EQUAL_UINT8(0U, fixture.erase_start_calls);

    fixture.authorized = 1U;
    PrepareReady();
    RequestArtifact(BOOT_SLOT_B_BASE_ADDRESS, 1040U);
    TEST_ASSERT_EQUAL(UDS_NRC_NONE, Handle(block, sizeof(block)));

    block[1] = 2U;
    fixture.write_succeeds = 0U;
    download.received_size = BOOT_IMAGE_HEADER_SIZE;
    TEST_ASSERT_EQUAL(UDS_NRC_GENERAL_PROGRAMMING_FAILURE,
                      Handle(block, 18U));
    TEST_ASSERT_EQUAL(UDS_DOWNLOAD_STATE_FAILED, download.stats.state);
    TEST_ASSERT_EQUAL_UINT8(1U, fixture.abort_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_erase_selects_only_inactive_slot_and_reports_progress);
    RUN_TEST(test_header_is_committed_only_after_payload_and_finalize);
    RUN_TEST(test_sequence_and_duplicate_blocks_do_not_double_program);
    RUN_TEST(test_access_range_and_backend_failures_are_fail_closed);
    return UNITY_END();
}
