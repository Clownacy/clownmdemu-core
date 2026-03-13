#include "fm-lfo.h"

void FM_LFO_Initialise(FM_LFO* const state)
{
	state->frequency = 0;
	state->amplitude_modulation = state->phase_modulation = 0;
	state->sub_counter = state->counter = 0;
	state->enabled = cc_false;
}

cc_bool FM_LFO_SetEnabled(FM_LFO* const state, const cc_bool enabled)
{
	if (state->enabled != enabled)
	{
		state->enabled = enabled;

		if (!enabled)
		{
			state->counter = state->phase_modulation = state->amplitude_modulation = 0;
			return cc_true;
		}
	}

	return cc_false;
}

cc_bool FM_LFO_Advance(FM_LFO* const state)
{
	static const cc_u8l thresholds[8] = {0x6C, 0x4D, 0x47, 0x43, 0x3E, 0x2C, 0x08, 0x05};

	const cc_u8l threshold = thresholds[state->frequency];

	/* A really awkward way of checking if a certain number of cycles has passed. */
	if ((state->sub_counter++ & threshold) == threshold)
	{
		state->sub_counter = 0;

		if (state->enabled)
		{
			const cc_u8f phase_modulation_divisor = 4;

			++state->counter;
			state->counter %= 0x80;

			state->phase_modulation = state->counter / phase_modulation_divisor;
			state->amplitude_modulation = state->counter * 2;

			if (state->amplitude_modulation >= 0x80)
				state->amplitude_modulation &= 0x7E;
			else
				state->amplitude_modulation ^= 0x7E;

			/* Signal that the phase modulation has changed. */
			return state->counter % phase_modulation_divisor == 0;
		}
	}

	return cc_false;
}
