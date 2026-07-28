#include "unity.h"
#include "can_transport.h"

/* Mock HAL_GetTick */
static uint32_t current_tick = 0;
uint32_t HAL_GetTick(void) { return current_tick; }

uint32_t mock_fdcan_fifo_size = 3;
uint32_t mock_fdcan_messages_added = 0;
HAL_StatusTypeDef mock_fdcan_status = HAL_OK;
FDCAN_HandleTypeDef hfdcan1;

uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *hfdcan)
{
    (void)hfdcan;

    if (mock_fdcan_messages_added >= mock_fdcan_fifo_size)
    {
        return 0U;
    }

    return mock_fdcan_fifo_size - mock_fdcan_messages_added;
}

HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *hfdcan, FDCAN_TxHeaderTypeDef *pTxHeader, uint8_t *pTxData)
{
    (void)hfdcan;
    (void)pTxHeader;
    (void)pTxData;

    if (mock_fdcan_status != HAL_OK)
    {
        return mock_fdcan_status;
    }

    mock_fdcan_messages_added++;
    return HAL_OK;
}

void setUp(void)
{
    mock_fdcan_messages_added = 0;
    mock_fdcan_fifo_size = 3;
    mock_fdcan_status = HAL_OK;
    current_tick = 0U;
    CAN_Transport_Init(&hfdcan1);
}

void tearDown(void) {}

void test_SendClassic_goes_direct_when_hw_fifo_free(void)
{
    uint8_t data[8] = {0};
    CAN_Transport_Result_t res = CAN_Transport_SendClassic(0x100, CAN_TRANSPORT_ID_STANDARD, data);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_OK, res);
    TEST_ASSERT_EQUAL(1, mock_fdcan_messages_added);
}

void test_SendClassic_queues_when_hw_fifo_full(void)
{
    uint8_t data[8] = {0};
    mock_fdcan_fifo_size = 0; // Hardware full

    CAN_Transport_Result_t res = CAN_Transport_SendClassic(0x100, CAN_TRANSPORT_ID_STANDARD, data);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_QUEUED, res);
    
    CAN_Transport_Stats_t stats;
    CAN_Transport_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT8(1U, stats.pending_count);
}

void test_SendClassicLatest_replaces_pending_same_id(void)
{
    uint8_t data1[8] = {1};
    uint8_t data2[8] = {2};
    mock_fdcan_fifo_size = 0; // Hardware full

    CAN_Transport_Result_t res1 = CAN_Transport_SendClassicLatest(0x100, CAN_TRANSPORT_ID_STANDARD, data1);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_QUEUED, res1);

    CAN_Transport_Result_t res2 = CAN_Transport_SendClassicLatest(0x100, CAN_TRANSPORT_ID_STANDARD, data2);
    TEST_ASSERT_EQUAL(CAN_TRANSPORT_QUEUED, res2);

    CAN_Transport_Stats_t stats;
    CAN_Transport_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT8(1U, stats.pending_count);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.coalesced);
}

void test_DisabledTransport_rejects_send_without_touching_hardware(void)
{
    uint8_t data[8] = {0};
    CAN_Transport_Stats_t stats;

    CAN_Transport_Init(NULL);

    TEST_ASSERT_EQUAL(
        CAN_TRANSPORT_NOT_INITIALIZED,
        CAN_Transport_SendClassic(
            0x100U,
            CAN_TRANSPORT_ID_STANDARD,
            data));

    CAN_Transport_Process();
    CAN_Transport_GetStats(&stats);
    TEST_ASSERT_EQUAL_UINT32(0U, mock_fdcan_messages_added);
    TEST_ASSERT_EQUAL_UINT8(0U, stats.pending_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_SendClassic_goes_direct_when_hw_fifo_free);
    RUN_TEST(test_SendClassic_queues_when_hw_fifo_full);
    RUN_TEST(test_SendClassicLatest_replaces_pending_same_id);
    RUN_TEST(
        test_DisabledTransport_rejects_send_without_touching_hardware);
    return UNITY_END();
}
