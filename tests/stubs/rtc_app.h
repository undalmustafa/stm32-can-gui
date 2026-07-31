#ifndef RTC_APP_H
#define RTC_APP_H

#include <stdint.h>

void CAN_Handle_RTC_Set_Time(uint8_t *data);
void CAN_Handle_RTC_Set_DateTime(uint8_t *data);
void CAN_Handle_RTC_Set_Alarm(uint8_t *data);

#endif /* RTC_APP_H */
