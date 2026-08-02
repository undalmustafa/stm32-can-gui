#ifndef APP_SAFE_STATE_H
#define APP_SAFE_STATE_H

/*
 * CALL_CONTEXT_DEFAULT: ISR_SAFE
 * CALL_CONTEXT_ISR_SAFE: all
 * CALL_CONTEXT_INTERNAL: none
 */

/*
 * Board-level emergency output path. Safe to call before HAL peripheral
 * initialization and from fault context with interrupts disabled.
 */
void App_SafeState_Engage(void);

#endif /* APP_SAFE_STATE_H */
