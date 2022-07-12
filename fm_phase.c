#include "fm_phase.h"

static void RecalculatePhaseStep(FM_Phase_State *phase)
{
	/* First, obtain some values. */

	/* Detune-related magic numbers. */
	static const unsigned int key_codes[0x10] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3};

	static const unsigned int detune_lookup[8][4][4] = {
		{
			{0,  0,  1,  2},
			{0,  0,  1,  2},
			{0,  0,  1,  2},
			{0,  0,  1,  2}
		},
		{
			{0,  1,  2,  2},
			{0,  1,  2,  3},
			{0,  1,  2,  3},
			{0,  1,  2,  3}
		},
		{
			{0,  1,  2,  4},
			{0,  1,  3,  4},
			{0,  1,  3,  4},
			{0,  1,  3,  5}
		},
		{
			{0,  2,  4,  5},
			{0,  2,  4,  6},
			{0,  2,  4,  6},
			{0,  2,  5,  7}
		},
		{
			{0,  2,  5,  8},
			{0,  3,  6,  8},
			{0,  3,  6,  9},
			{0,  3,  7, 10}
		},
		{
			{0,  4,  8, 11},
			{0,  4,  8, 12},
			{0,  4,  9, 13},
			{0,  5, 10, 14}
		},
		{
			{0,  5, 11, 16},
			{0,  6, 12, 17},
			{0,  6, 13, 19},
			{0,  7, 14, 20}
		},
		{
			{0,  8, 16, 22},
			{0,  8, 16, 22},
			{0,  8, 16, 22},
			{0,  8, 16, 22}
		}
	};

	/* The octave. */
	const unsigned int block = (phase->f_number_and_block >> 11) & 7;

	/* The frequency of the note within the octave. */
	const unsigned int f_number = phase->f_number_and_block & 0x7FF;

	/* Frequency offset. */
	const unsigned int detune = detune_lookup[block][key_codes[f_number >> 7]][phase->detune & 3];

	/* Finally, calculate the phase step. */

	/* Start by basing the step on the F-number. */
	phase->step = f_number;

	/* TODO: According to Nemesis (http://gendev.spritesmind.net/forum/viewtopic.php?p=8908#p8908),
	   the LFO frequency modulation should be applied here. Just add it and then apply an 11-bit mask. */

	/* Then apply the octave to the step. */
	/* Octave 0 is 0.5x the frequency, 1 is 1x, 2 is 2x, 3 is 4x, etc. */
	phase->step <<= block;
	phase->step >>= 1;

	/* Apply the detune. */
	if ((phase->detune & 4) != 0)
		phase->step -= detune;
	else
		phase->step += detune;

	/* Apply the multiplier. */
	/* Multiplier 0 is 0.5x the frequency, 1 is 1x, 2 is 2x, 3 is 3x, etc. */
	phase->step *= phase->multiplier;
	phase->step /= 2;

	/* Emulate the 20-bit overflow that could occur here on a real YM2612. */
	phase->step &= 0xFFFFF;
}

void FM_Phase_State_Initialise(FM_Phase_State *phase)
{
	FM_Phase_SetFrequency(phase, 0);
	FM_Phase_SetDetuneAndMultiplier(phase, 0, 0);
	FM_Phase_Reset(phase);
}

unsigned int FM_Phase_GetKeyCode(const FM_Phase_State *phase)
{
	return phase->key_code;
}

void FM_Phase_SetFrequency(FM_Phase_State *phase, unsigned int f_number_and_block)
{
	phase->f_number_and_block = f_number_and_block;
	phase->key_code = f_number_and_block >> 9;

	RecalculatePhaseStep(phase);
}

void FM_Phase_SetDetuneAndMultiplier(FM_Phase_State *phase, unsigned int detune, unsigned int multiplier)
{
	phase->detune = detune;
	phase->multiplier = multiplier == 0 ? 1 : multiplier * 2;

	RecalculatePhaseStep(phase);
}

void FM_Phase_Reset(FM_Phase_State *phase)
{
	phase->position = 0;
}

unsigned int FM_Phase_Increment(FM_Phase_State *phase)
{
	phase->position += phase->step;

	return phase->position;
}
