/* http://gendev.spritesmind.net/forum/viewtopic.php?p=6177#p6177 */

#include "fm-phase.h"

#include "clowncommon/clowncommon.h"

static void RecalculatePhaseStep(FM_Phase_State* const phase, const FM_LFO* const lfo, const cc_u8f phase_modulation_sensitivity)
{
	/* First, obtain some values. */

	/* Detune-related magic numbers. */
	static const cc_u16f key_codes[0x10] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3};

	static const cc_u16f detune_lookup[8][4][4] = {
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
	const cc_u16f block = (phase->f_number_and_block >> 11) & 7;

	/* The frequency of the note within the octave. */
	const cc_u16f f_number = phase->f_number_and_block & 0x7FF;

	/* Frequency offset. */
	const cc_u16f detune = detune_lookup[block][key_codes[f_number >> 7]][phase->detune % CC_COUNT_OF(detune_lookup[0][0])];

	/* The phase modulation is a wave, which is made of the same quadrant mirrored repeatedly. */
	const cc_bool phase_modulation_is_negative_lobe = (lfo->phase_modulation & 0x10) != 0;
	const cc_bool phase_modulation_is_mirrored_size_of_lobe = (lfo->phase_modulation & 8) != 0;
	const cc_u8f phase_modulation_absolute_quadrant = (phase_modulation_is_mirrored_size_of_lobe ? -lfo->phase_modulation : lfo->phase_modulation) & 7;

	/* Finally, calculate the phase step. */

	/* Start by basing the step on the F-number, mutated by the phase modulation. */

#if 1
	/* This goofy thing implements a fixed-point multiplication using only shifts and an addition.
	   Unfortunately, this particular method is required in order to recreate the rounding errors of a real YM2612,
	   which prevents me from replacing it with a real multiplication without sacrificing accuracy. */
	static const cc_u8l lfo_shift_lookup[8][8][2] = {
		{{7, 7}, {7, 7}, {7, 7}, {7, 7}, {7, 7}, {7, 7}, {7, 7}, {7, 7}},
		{{7, 7}, {7, 7}, {7, 7}, {7, 7}, {7, 2}, {7, 2}, {7, 2}, {7, 2}},
		{{7, 7}, {7, 7}, {7, 7}, {7, 2}, {7, 2}, {7, 2}, {1, 7}, {1, 7}},
		{{7, 7}, {7, 7}, {7, 2}, {7, 2}, {1, 7}, {1, 7}, {1, 2}, {1, 2}},
		{{7, 7}, {7, 7}, {7, 2}, {1, 7}, {1, 7}, {1, 7}, {1, 2}, {0, 7}},
		{{7, 7}, {7, 7}, {1, 7}, {1, 2}, {0, 7}, {0, 7}, {0, 2}, {0, 1}},
		{{7, 7}, {7, 7}, {1, 7}, {1, 2}, {0, 7}, {0, 7}, {0, 2}, {0, 1}},
		{{7, 7}, {7, 7}, {1, 7}, {1, 2}, {0, 7}, {0, 7}, {0, 2}, {0, 1}},
	};

	const cc_u16f f_number_upper_nybbles = f_number >> 4;
	const cc_u8l* const shifts = lfo_shift_lookup[phase_modulation_sensitivity][phase_modulation_absolute_quadrant];
	phase->step = (f_number_upper_nybbles >> shifts[0]) + (f_number_upper_nybbles >> shifts[1]);
#else
	/* This is what the above code is an approximation of. */
	static const cc_u8l lfo_lookup[8][8] = {
		{0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
		{0x01, 0x01, 0x01, 0x01, 0x20, 0x20, 0x20, 0x20},
		{0x01, 0x01, 0x01, 0x20, 0x20, 0x20, 0x40, 0x40},
		{0x01, 0x01, 0x20, 0x20, 0x40, 0x40, 0x60, 0x60},
		{0x01, 0x01, 0x20, 0x40, 0x40, 0x40, 0x60, 0x80},
		{0x01, 0x01, 0x40, 0x60, 0x80, 0x80, 0xA0, 0xC0},
		{0x01, 0x01, 0x40, 0x60, 0x80, 0x80, 0xA0, 0xC0},
		{0x01, 0x01, 0x40, 0x60, 0x80, 0x80, 0xA0, 0xC0}
	};

	phase->step = f_number * lfo_lookup[phase_modulation_sensitivity][phase_modulation_absolute_quadrant] / 0x800;
#endif

	if (phase_modulation_sensitivity > 5)
		phase->step <<= phase_modulation_sensitivity - 5;

	phase->step >>= 2;

	if (phase_modulation_is_negative_lobe)
		phase->step = -phase->step;

	/* Mix-in the unmodified F-number. */
	phase->step += f_number << 1; /* Note that the F-number is converted to 16-bit here. */
	phase->step &= 0xFFF;

	/* Then apply the octave to the step. */
	/* Octave 0 is 0.5x the frequency, 1 is 1x, 2 is 2x, 3 is 4x, etc. */
	phase->step <<= block;
	phase->step >>= 1;

	/* Convert from 16-bit to 15-bit. */
	phase->step >>= 1;

	/* Apply the detune. */
	if ((phase->detune & 4) != 0)
		phase->step -= detune;
	else
		phase->step += detune;

	/* Emulate the detune underflow bug. This fixes Comix Zone's Track 5 and many other GEMS games. */
	/* https://gendev.spritesmind.net/forum/viewtopic.php?p=6177#p6177 */
	phase->step &= 0x1FFFF;

	/* Apply the multiplier. */
	/* Multiplier 0 is 0.5x the frequency, 1 is 1x, 2 is 2x, 3 is 3x, etc. */
	phase->step *= phase->multiplier;
	phase->step /= 2;
}

void FM_Phase_State_Initialise(FM_Phase_State* const phase)
{
	FM_Phase_SetFrequency(phase, 0);
	FM_Phase_SetDetuneAndMultiplier(phase, 0, 0);
	FM_Phase_Reset(phase);
}

cc_u16f FM_Phase_GetKeyCode(const FM_Phase_State* const phase)
{
	return phase->key_code;
}

void FM_Phase_SetFrequency(FM_Phase_State* const phase, const cc_u16f f_number_and_block)
{
	phase->f_number_and_block = f_number_and_block;
	phase->key_code = f_number_and_block >> 9;

	/*RecalculatePhaseStep(phase);*/
}

void FM_Phase_SetDetuneAndMultiplier(FM_Phase_State* const phase, const cc_u16f detune, const cc_u16f multiplier)
{
	phase->detune = detune;
	phase->multiplier = multiplier == 0 ? 1 : multiplier * 2;

	/*RecalculatePhaseStep(phase);*/
}

void FM_Phase_Reset(FM_Phase_State* const phase)
{
	phase->position = 0;
}

cc_u32f FM_Phase_Increment(FM_Phase_State* const phase, const FM_LFO* const lfo, const cc_u8f phase_modulation_sensitivity)
{
	/* TODO: Only call this here when LFO is enabled. */
	RecalculatePhaseStep(phase, lfo, phase_modulation_sensitivity);

	phase->position += phase->step;

	return phase->position;
}
