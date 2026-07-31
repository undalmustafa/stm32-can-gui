#ifndef CAN_APP_INIT_H
#define CAN_APP_INIT_H

#include "can_app.h"
#include "stm32h7xx_hal.h"

/*
 * Main-loop only. Owns the FDCAN initialization transaction, CAN application
 * availability state, transport binding and failure cleanup/logging.
 */
CAN_App_InitResult_t CAN_App_InitHardware(FDCAN_HandleTypeDef *hfdcan,
                                          uint8_t peripheral_ready);

#endif /* CAN_APP_INIT_H */
