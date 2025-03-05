#include "low-pass-filter.h"

void LowPassFilter_Initialise(LowPassFilterState* const states, const cc_u8f total_channels)
{
	size_t i;

	for (i = 0; i < total_channels; ++i)
	{
		LowPassFilterState* const state = &states[i];

		state->previous_sample = state->previous_output = 0;
	}
}

void LowPassFilter_Apply(LowPassFilterState* const states, const cc_u8f total_channels, cc_s16l* const sample_buffer, const size_t total_frames, const cc_u32f magic1, const cc_u32f magic2)
{
	size_t current_frame;

	cc_s16l *sample_pointer = sample_buffer;

	for (current_frame = 0; current_frame < total_frames; ++current_frame)
	{
		cc_u8f current_channel;

		for (current_channel = 0; current_channel < total_channels; ++current_channel)
		{
			LowPassFilterState* const state = &states[current_channel];
			const cc_s16l output = (((cc_s32f)*sample_pointer + state->previous_sample) * magic1 + state->previous_output * magic2) / LOW_PASS_FILTER_FIXED_BASE;

			state->previous_sample = *sample_pointer;
			state->previous_output = output;

			*sample_pointer = output;
			++sample_pointer;
		}
	}
}
