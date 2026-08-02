#ifndef TIC12400_SWITCH_H
#define TIC12400_SWITCH_H

/*
 * CALL_CONTEXT_DEFAULT: INTERNAL
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: all
 */

#include <stdint.h>

#define TIC12400_SWITCH_CHANNEL_COUNT        24U
#define TIC12400_SWITCH_MASK                 0x00FFFFFFUL

typedef struct
{
    uint32_t stable_closed_mask;
    uint32_t valid_mask;
    uint32_t last_change_mask;
    uint8_t generation;
} TIC12400_SwitchFilter_t;

void TIC12400_SwitchFilter_Init(TIC12400_SwitchFilter_t *filter);

uint8_t TIC12400_SwitchFilter_CommitDebouncedMask(
    TIC12400_SwitchFilter_t *filter,
    uint32_t closed_mask,
    uint32_t fitted_mask);

#endif /* TIC12400_SWITCH_H */
