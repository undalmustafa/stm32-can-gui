#ifndef TIC12400_SWITCH_H
#define TIC12400_SWITCH_H

#include <stdint.h>

#define TIC12400_SWITCH_CHANNEL_COUNT        24U
#define TIC12400_SWITCH_CLOSED_MAX_ADC_CODE  512U
#define TIC12400_SWITCH_DEBOUNCE_SAMPLES     3U
#define TIC12400_SWITCH_MASK                 0x00FFFFFFUL

typedef struct
{
    uint32_t candidate_closed_mask;
    uint32_t stable_closed_mask;
    uint32_t valid_mask;
    uint32_t last_change_mask;
    uint8_t candidate_count[TIC12400_SWITCH_CHANNEL_COUNT];
    uint8_t generation;
} TIC12400_SwitchFilter_t;

void TIC12400_SwitchFilter_Init(TIC12400_SwitchFilter_t *filter);

uint8_t TIC12400_SwitchFilter_Update(
    TIC12400_SwitchFilter_t *filter,
    const uint16_t adc_code[TIC12400_SWITCH_CHANNEL_COUNT],
    uint32_t fitted_mask);

uint8_t TIC12400_SwitchFilter_CommitDebouncedMask(
    TIC12400_SwitchFilter_t *filter,
    uint32_t closed_mask,
    uint32_t fitted_mask);

#endif /* TIC12400_SWITCH_H */
