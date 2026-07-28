#ifndef TIC12400_CAN_H
#define TIC12400_CAN_H

#include "can_transport.h"

#include <stdint.h>

typedef struct
{
    uint32_t status_frames_accepted;
    uint32_t adc_frames_accepted;
    uint32_t tx_failures;
    CAN_Transport_Result_t last_tx_result;
    uint8_t adc_generation;
    uint8_t next_adc_group;
} TIC12400_CanSnapshot_t;

extern volatile TIC12400_CanSnapshot_t g_tic12400_can;

void TIC12400_CAN_Init(void);
void TIC12400_CAN_Process(void);

#endif /* TIC12400_CAN_H */
