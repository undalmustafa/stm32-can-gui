#ifndef TIC12400_PROBE_H
#define TIC12400_PROBE_H

#include "tic12400.h"

#include <stdint.h>

typedef struct
{
    uint32_t attempts;
    uint32_t interrupt_count;
    uint32_t tx_frame;
    uint32_t rx_frame;
    uint32_t register_data;
    uint32_t int_status;
    uint32_t hal_error;
    TIC12400_Result_t result;
    HAL_StatusTypeDef hal_status;
    TIC12400_StatusFlags_t status;
    uint8_t device_id;
    uint8_t online;
    uint8_t por_observed;
    uint8_t interrupt_pending;
} TIC12400_ProbeSnapshot_t;

extern volatile TIC12400_ProbeSnapshot_t g_tic12400_probe;

void TIC12400_Probe_Init(SPI_HandleTypeDef *spi);
void TIC12400_Probe_NotifyInterruptFromIsr(void);

#endif /* TIC12400_PROBE_H */
