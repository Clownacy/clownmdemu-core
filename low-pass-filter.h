#ifndef LOW_PASS_FILTER_H
#define LOW_PASS_FILTER_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

#define LOW_PASS_FILTER_FIXED_BASE 0x10000
#define LOW_PASS_FILTER_COMPUTE_FIXED(x, COEFFICIENT_B) (cc_u16l)CC_DIVIDE_ROUND(x * LOW_PASS_FILTER_FIXED_BASE, COEFFICIENT_B)
#define LOW_PASS_FILTER_COMPUTE_MAGIC(COEFFICIENT_A, COEFFICIENT_B) LOW_PASS_FILTER_COMPUTE_FIXED(1.0, COEFFICIENT_B), LOW_PASS_FILTER_COMPUTE_FIXED(COEFFICIENT_A, COEFFICIENT_B)

typedef struct LowPassFilterState
{
	cc_s16l previous_sample, previous_output;
} LowPassFilterState;

void LowPassFilter_Initialise(LowPassFilterState *states, cc_u8f total_channels);
void LowPassFilter_Apply(LowPassFilterState *states, cc_u8f total_channels, cc_s16l *sample_buffer, size_t total_frames, cc_u32f magic1, cc_u32f magic2);

#endif /* LOW_PASS_FILTER_H */
