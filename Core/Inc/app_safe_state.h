#ifndef APP_SAFE_STATE_H
#define APP_SAFE_STATE_H

/*
 * Board-level emergency output path. Safe to call before HAL peripheral
 * initialization and from fault context with interrupts disabled.
 */
void App_SafeState_Engage(void);

#endif /* APP_SAFE_STATE_H */
