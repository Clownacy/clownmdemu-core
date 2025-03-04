#include "low-pass-filter.h"

void LowPassFilter_Initialise(LowPassFilterState* const state)
{
	state->previous_sample = state->previous_output = 0;
}

void LowPassFilter_Apply(LowPassFilterState* const state, cc_s16l* const sample_buffer, const size_t total_frames, const cc_u8f stride, const cc_u32f magic1, const cc_u32f magic2)
{
	size_t i;

	for (i = 0; i < total_frames * stride; i += stride)
	{
		const cc_s16l output = (((cc_s32f)sample_buffer[i] + state->previous_sample) * magic1 + state->previous_output * magic2) / LOW_PASS_FILTER_FIXED_BASE;

		state->previous_sample = sample_buffer[i];
		state->previous_output = output;

		sample_buffer[i] = output;
	}
}
