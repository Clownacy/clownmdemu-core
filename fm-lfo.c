#include "fm-lfo.h"

void FM_LFO_Initialise(FM_LFO* const state)
{
	state->frequency = 0;
	state->amplitude_modulation = state->phase_modulation = 0;
	state->sub_counter = state->counter = 0;
	state->enabled = cc_false;
}

void FM_LFO_Advance(FM_LFO* const state)
{
	static const cc_u8l thresholds[8] = {0x6C, 0x4D, 0x47, 0x43, 0x3E, 0x2C, 0x08, 0x05};

	const cc_u8l threshold = thresholds[state->frequency];

	state->phase_modulation = state->counter / 4;
	state->amplitude_modulation = state->counter * 2;

	if (state->amplitude_modulation >= 0x80)
		state->amplitude_modulation &= 0x7E;
	else
		state->amplitude_modulation ^= 0x7E;

	/* TODO: What the hell is this actually trying to do? */
	if ((state->sub_counter++ & threshold) == threshold)
	{
		state->sub_counter = 0;
		++state->counter;
		state->counter %= 0x80;
	}

	if (!state->enabled)
		state->counter = 0;
}
