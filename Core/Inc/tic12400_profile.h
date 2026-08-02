#ifndef TIC12400_PROFILE_H
#define TIC12400_PROFILE_H

/*
 * CALL_CONTEXT_DEFAULT: INTERNAL
 * CALL_CONTEXT_ISR_SAFE: none
 * CALL_CONTEXT_INTERNAL: all
 */

#include <stdint.h>

#define TIC12400_PROFILE_CHANNEL_MASK          0x00FFFFFFUL
#define TIC12400_PROFILE_BATTERY_CAPABLE_MASK  0x000003FFUL
#define TIC12400_PROFILE_CARRIER_FITTED_MASK   0x00FFEFFFUL

typedef struct
{
    uint32_t enabled_mask;
    uint32_t battery_switch_mask;
    uint32_t adc_mode_mask;
} TIC12400_Profile_t;

typedef enum
{
    TIC12400_PROFILE_OK = 0,
    TIC12400_PROFILE_INVALID_ARGUMENT,
    TIC12400_PROFILE_INPUT_NOT_FITTED,
    TIC12400_PROFILE_BATTERY_INPUT_UNSUPPORTED,
    TIC12400_PROFILE_MODE_INPUT_DISABLED
} TIC12400_ProfileResult_t;

TIC12400_Profile_t TIC12400_Profile_CarrierBinary(void);

TIC12400_ProfileResult_t TIC12400_Profile_Validate(
    const TIC12400_Profile_t *profile,
    uint32_t fitted_mask);

uint32_t TIC12400_Profile_DecodeComparatorClosed(
    const TIC12400_Profile_t *profile,
    uint32_t comparator_above_mask);

uint32_t TIC12400_Profile_BuildComparatorInterruptEnable(
    const TIC12400_Profile_t *profile,
    uint8_t first_channel);

#endif /* TIC12400_PROFILE_H */
