#include "tic12400_profile.h"

#include <stddef.h>

TIC12400_Profile_t TIC12400_Profile_CarrierBinary(void)
{
    TIC12400_Profile_t profile = {
        .enabled_mask = TIC12400_PROFILE_CARRIER_FITTED_MASK,
        .battery_switch_mask = 0U,
        .adc_mode_mask = 0U,
    };

    return profile;
}

TIC12400_ProfileResult_t TIC12400_Profile_Validate(
    const TIC12400_Profile_t *profile,
    uint32_t fitted_mask)
{
    uint32_t fitted;

    if (profile == NULL)
    {
        return TIC12400_PROFILE_INVALID_ARGUMENT;
    }

    fitted = fitted_mask & TIC12400_PROFILE_CHANNEL_MASK;
    if ((profile->enabled_mask & ~fitted) != 0U)
    {
        return TIC12400_PROFILE_INPUT_NOT_FITTED;
    }

    if ((profile->battery_switch_mask &
         ~TIC12400_PROFILE_BATTERY_CAPABLE_MASK) != 0U)
    {
        return TIC12400_PROFILE_BATTERY_INPUT_UNSUPPORTED;
    }

    if (((profile->battery_switch_mask |
          profile->adc_mode_mask) &
         ~profile->enabled_mask) != 0U)
    {
        return TIC12400_PROFILE_MODE_INPUT_DISABLED;
    }

    return TIC12400_PROFILE_OK;
}

uint32_t TIC12400_Profile_DecodeComparatorClosed(
    const TIC12400_Profile_t *profile,
    uint32_t comparator_above_mask)
{
    uint32_t battery_inputs;
    uint32_t enabled;
    uint32_t ground_inputs;

    if (profile == NULL)
    {
        return 0U;
    }

    enabled =
        profile->enabled_mask & TIC12400_PROFILE_CHANNEL_MASK;
    battery_inputs =
        profile->battery_switch_mask & enabled;
    ground_inputs = enabled & ~battery_inputs;
    comparator_above_mask &= TIC12400_PROFILE_CHANNEL_MASK;

    /*
     * Current-source ground switches are closed below threshold. Current-sink
     * battery switches are closed above threshold.
     */
    return (((~comparator_above_mask) & ground_inputs) |
            (comparator_above_mask & battery_inputs)) &
           enabled;
}

uint32_t TIC12400_Profile_BuildComparatorInterruptEnable(
    const TIC12400_Profile_t *profile,
    uint8_t first_channel)
{
    uint32_t comparator_inputs;
    uint32_t value = 0U;
    uint8_t channel;
    uint8_t field;

    if ((profile == NULL) ||
        ((first_channel != 0U) && (first_channel != 12U)))
    {
        return 0U;
    }

    comparator_inputs =
        profile->enabled_mask & ~profile->adc_mode_mask;
    for (field = 0U; field < 12U; field++)
    {
        channel = (uint8_t)(first_channel + field);
        if ((comparator_inputs & (1UL << channel)) != 0U)
        {
            value |= 3UL << (field * 2U);
        }
    }

    return value;
}
