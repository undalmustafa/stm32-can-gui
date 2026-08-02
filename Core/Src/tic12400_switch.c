#include "tic12400_switch.h"

#include <stddef.h>

void TIC12400_SwitchFilter_Init(TIC12400_SwitchFilter_t *filter)
{
    if (filter == NULL)
    {
        return;
    }

    filter->stable_closed_mask = 0U;
    filter->valid_mask = 0U;
    filter->last_change_mask = 0U;
    filter->generation = 0U;
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
