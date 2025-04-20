#include "low-pass-filter.h"

#include <string.h>

void LowPassFilter_SecondOrder_Initialise(LowPassFilter_SecondOrder_State* const states, const cc_u8f total_channels)
{
	memset(states, 0, sizeof(*states) * total_channels);
}

void LowPassFilter_SecondOrder_Apply(LowPassFilter_SecondOrder_State* const states, const cc_u8f total_channels, cc_s16l* const sample_buffer, const size_t total_frames, const cc_s32f sample_magic_1, const cc_s32f sample_magic_2, const cc_s32f output_magic_1, const cc_s32f output_magic_2)
{
	size_t current_frame;

	cc_s16l *sample_pointer = sample_buffer;

	for (current_frame = 0; current_frame < total_frames; ++current_frame)
	{
		cc_u8f current_channel;

		for (current_channel = 0; current_channel < total_channels; ++current_channel)
		{
			LowPassFilter_SecondOrder_State* const state = &states[current_channel];

			const cc_s32f unclamped_output
				= LOW_PASS_FILTER_FIXED_MULTIPLY((cc_s32f)*sample_pointer + state->previous_samples[0], sample_magic_1)
				+ LOW_PASS_FILTER_FIXED_MULTIPLY((cc_s32f)state->previous_samples[0] + state->previous_samples[1], sample_magic_2)
				+ LOW_PASS_FILTER_FIXED_MULTIPLY(state->previous_outputs[0], output_magic_1)
				- LOW_PASS_FILTER_FIXED_MULTIPLY(state->previous_outputs[1], output_magic_2);

			/* For some reason, out-of-range values can be produced by the low-pass filter. */
			const cc_s16l output = CC_CLAMP(-0x7FFF, 0x7FFF, unclamped_output);

			state->previous_samples[1] = state->previous_samples[0];
			state->previous_samples[0] = *sample_pointer;
			state->previous_outputs[1] = state->previous_outputs[0];
			state->previous_outputs[0] = output;

			*sample_pointer = output;
			++sample_pointer;
		}
	}
}
