#include "tic12400_switch.h"

#include <stddef.h>

void TIC12400_SwitchFilter_Init(TIC12400_SwitchFilter_t *filter)
{
    uint8_t channel;

    if (filter == NULL)
    {
        return;
    }

    filter->candidate_closed_mask = 0U;
    filter->stable_closed_mask = 0U;
    filter->valid_mask = 0U;
    filter->last_change_mask = 0U;
    filter->generation = 0U;

    for (channel = 0U;
         channel < TIC12400_SWITCH_CHANNEL_COUNT;
         channel++)
    {
        filter->candidate_count[channel] = 0U;
    }
}

uint8_t TIC12400_SwitchFilter_Update(
    TIC12400_SwitchFilter_t *filter,
    const uint16_t adc_code[TIC12400_SWITCH_CHANNEL_COUNT],
    uint32_t fitted_mask)
{
    uint32_t bit;
    uint32_t fitted;
    uint8_t channel;
    uint8_t candidate_closed;
    uint8_t observed_closed;
    uint8_t state_updated = 0U;

    if ((filter == NULL) || (adc_code == NULL))
    {
        return 0U;
    }

    fitted = fitted_mask & TIC12400_SWITCH_MASK;
    filter->last_change_mask = 0U;

    for (channel = 0U;
         channel < TIC12400_SWITCH_CHANNEL_COUNT;
         channel++)
    {
        bit = 1UL << channel;
        if ((fitted & bit) == 0U)
        {
            filter->candidate_count[channel] = 0U;
            filter->candidate_closed_mask &= ~bit;
            filter->stable_closed_mask &= ~bit;
            filter->valid_mask &= ~bit;
            continue;
        }

        observed_closed =
            (adc_code[channel] <=
             TIC12400_SWITCH_CLOSED_MAX_ADC_CODE) ? 1U : 0U;
        candidate_closed =
            ((filter->candidate_closed_mask & bit) != 0U) ? 1U : 0U;

        if ((filter->candidate_count[channel] == 0U) ||
            (candidate_closed != observed_closed))
        {
            if (observed_closed != 0U)
            {
                filter->candidate_closed_mask |= bit;
            }
            else
            {
                filter->candidate_closed_mask &= ~bit;
            }
            filter->candidate_count[channel] = 1U;
        }
        else if (filter->candidate_count[channel] <
                 TIC12400_SWITCH_DEBOUNCE_SAMPLES)
        {
            filter->candidate_count[channel]++;
        }

        if (filter->candidate_count[channel] <
            TIC12400_SWITCH_DEBOUNCE_SAMPLES)
        {
            continue;
        }

        if ((filter->valid_mask & bit) == 0U)
        {
            if (observed_closed != 0U)
            {
                filter->stable_closed_mask |= bit;
            }
            else
            {
                filter->stable_closed_mask &= ~bit;
            }
            filter->valid_mask |= bit;
            state_updated = 1U;
        }
        else if ((((filter->stable_closed_mask & bit) != 0U) ? 1U : 0U)
                 != observed_closed)
        {
            if (observed_closed != 0U)
            {
                filter->stable_closed_mask |= bit;
            }
            else
            {
                filter->stable_closed_mask &= ~bit;
            }
            filter->last_change_mask |= bit;
            state_updated = 1U;
        }
    }

    filter->candidate_closed_mask &= fitted;
    filter->stable_closed_mask &= fitted;
    filter->valid_mask &= fitted;
    filter->last_change_mask &= fitted;

    if (state_updated != 0U)
    {
        filter->generation++;
    }

    return state_updated;
}

uint8_t TIC12400_SwitchFilter_CommitDebouncedMask(
    TIC12400_SwitchFilter_t *filter,
    uint32_t closed_mask,
    uint32_t fitted_mask)
{
    uint32_t changed_mask;
    uint32_t fitted;

    if (filter == NULL)
    {
        return 0U;
    }

    fitted = fitted_mask & TIC12400_SWITCH_MASK;
    closed_mask &= fitted;
    filter->candidate_closed_mask = closed_mask;
    filter->last_change_mask = 0U;

    if (filter->valid_mask != fitted)
    {
        filter->stable_closed_mask = closed_mask;
        filter->valid_mask = fitted;
        filter->generation++;
        return 1U;
    }

    changed_mask =
        (filter->stable_closed_mask ^ closed_mask) & fitted;
    if (changed_mask == 0U)
    {
        return 0U;
    }

    filter->stable_closed_mask = closed_mask;
    filter->last_change_mask = changed_mask;
    filter->generation++;
    return 1U;
}
