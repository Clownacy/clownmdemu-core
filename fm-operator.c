/* Based on Nemesis's notes: http://gendev.spritesmind.net/forum/viewtopic.php?p=6114#p6114 */

/* http://gendev.spritesmind.net/forum/viewtopic.php?p=5716#p5716 */
/* http://gendev.spritesmind.net/forum/viewtopic.php?p=7967#p7967 */

#include "fm-operator.h"

#include <assert.h>
#include <math.h>

#include "clowncommon/clowncommon.h"

static const cc_u16l logarithmic_attenuation_sine_table[0x100] = {
	0x859, 0x6C3, 0x607, 0x58B, 0x52E, 0x4E4, 0x4A6, 0x471, 0x443, 0x41A, 0x3F5, 0x3D3, 0x3B5, 0x398, 0x37E, 0x365,
	0x34E, 0x339, 0x324, 0x311, 0x2FF, 0x2ED, 0x2DC, 0x2CD, 0x2BD, 0x2AF, 0x2A0, 0x293, 0x286, 0x279, 0x26D, 0x261,
	0x256, 0x24B, 0x240, 0x236, 0x22C, 0x222, 0x218, 0x20F, 0x206, 0x1FD, 0x1F5, 0x1EC, 0x1E4, 0x1DC, 0x1D4, 0x1CD,
	0x1C5, 0x1BE, 0x1B7, 0x1B0, 0x1A9, 0x1A2, 0x19B, 0x195, 0x18F, 0x188, 0x182, 0x17C, 0x177, 0x171, 0x16B, 0x166,
	0x160, 0x15B, 0x155, 0x150, 0x14B, 0x146, 0x141, 0x13C, 0x137, 0x133, 0x12E, 0x129, 0x125, 0x121, 0x11C, 0x118,
	0x114, 0x10F, 0x10B, 0x107, 0x103, 0x0FF, 0x0FB, 0x0F8, 0x0F4, 0x0F0, 0x0EC, 0x0E9, 0x0E5, 0x0E2, 0x0DE, 0x0DB,
	0x0D7, 0x0D4, 0x0D1, 0x0CD, 0x0CA, 0x0C7, 0x0C4, 0x0C1, 0x0BE, 0x0BB, 0x0B8, 0x0B5, 0x0B2, 0x0AF, 0x0AC, 0x0A9,
	0x0A7, 0x0A4, 0x0A1, 0x09F, 0x09C, 0x099, 0x097, 0x094, 0x092, 0x08F, 0x08D, 0x08A, 0x088, 0x086, 0x083, 0x081,
	0x07F, 0x07D, 0x07A, 0x078, 0x076, 0x074, 0x072, 0x070, 0x06E, 0x06C, 0x06A, 0x068, 0x066, 0x064, 0x062, 0x060,
	0x05E, 0x05C, 0x05B, 0x059, 0x057, 0x055, 0x053, 0x052, 0x050, 0x04E, 0x04D, 0x04B, 0x04A, 0x048, 0x046, 0x045,
	0x043, 0x042, 0x040, 0x03F, 0x03E, 0x03C, 0x03B, 0x039, 0x038, 0x037, 0x035, 0x034, 0x033, 0x031, 0x030, 0x02F,
	0x02E, 0x02D, 0x02B, 0x02A, 0x029, 0x028, 0x027, 0x026, 0x025, 0x024, 0x023, 0x022, 0x021, 0x020, 0x01F, 0x01E,
	0x01D, 0x01C, 0x01B, 0x01A, 0x019, 0x018, 0x017, 0x017, 0x016, 0x015, 0x014, 0x014, 0x013, 0x012, 0x011, 0x011,
	0x010, 0x00F, 0x00F, 0x00E, 0x00D, 0x00D, 0x00C, 0x00C, 0x00B, 0x00A, 0x00A, 0x009, 0x009, 0x008, 0x008, 0x007,
	0x007, 0x007, 0x006, 0x006, 0x005, 0x005, 0x005, 0x004, 0x004, 0x004, 0x003, 0x003, 0x003, 0x002, 0x002, 0x002,
	0x002, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
};

static const cc_u16l power_table[0x100] = {
	0x7FA, 0x7F5, 0x7EF, 0x7EA, 0x7E4, 0x7DF, 0x7DA, 0x7D4, 0x7CF, 0x7C9, 0x7C4, 0x7BF, 0x7B9, 0x7B4, 0x7AE, 0x7A9,
	0x7A4, 0x79F, 0x799, 0x794, 0x78F, 0x78A, 0x784, 0x77F, 0x77A, 0x775, 0x770, 0x76A, 0x765, 0x760, 0x75B, 0x756,
	0x751, 0x74C, 0x747, 0x742, 0x73D, 0x738, 0x733, 0x72E, 0x729, 0x724, 0x71F, 0x71A, 0x715, 0x710, 0x70B, 0x706,
	0x702, 0x6FD, 0x6F8, 0x6F3, 0x6EE, 0x6E9, 0x6E5, 0x6E0, 0x6DB, 0x6D6, 0x6D2, 0x6CD, 0x6C8, 0x6C4, 0x6BF, 0x6BA,
	0x6B5, 0x6B1, 0x6AC, 0x6A8, 0x6A3, 0x69E, 0x69A, 0x695, 0x691, 0x68C, 0x688, 0x683, 0x67F, 0x67A, 0x676, 0x671,
	0x66D, 0x668, 0x664, 0x65F, 0x65B, 0x657, 0x652, 0x64E, 0x649, 0x645, 0x641, 0x63C, 0x638, 0x634, 0x630, 0x62B,
	0x627, 0x623, 0x61E, 0x61A, 0x616, 0x612, 0x60E, 0x609, 0x605, 0x601, 0x5FD, 0x5F9, 0x5F5, 0x5F0, 0x5EC, 0x5E8,
	0x5E4, 0x5E0, 0x5DC, 0x5D8, 0x5D4, 0x5D0, 0x5CC, 0x5C8, 0x5C4, 0x5C0, 0x5BC, 0x5B8, 0x5B4, 0x5B0, 0x5AC, 0x5A8,
	0x5A4, 0x5A0, 0x59C, 0x599, 0x595, 0x591, 0x58D, 0x589, 0x585, 0x581, 0x57E, 0x57A, 0x576, 0x572, 0x56F, 0x56B,
	0x567, 0x563, 0x560, 0x55C, 0x558, 0x554, 0x551, 0x54D, 0x549, 0x546, 0x542, 0x53E, 0x53B, 0x537, 0x534, 0x530,
	0x52C, 0x529, 0x525, 0x522, 0x51E, 0x51B, 0x517, 0x514, 0x510, 0x50C, 0x509, 0x506, 0x502, 0x4FF, 0x4FB, 0x4F8,
	0x4F4, 0x4F1, 0x4ED, 0x4EA, 0x4E7, 0x4E3, 0x4E0, 0x4DC, 0x4D9, 0x4D6, 0x4D2, 0x4CF, 0x4CC, 0x4C8, 0x4C5, 0x4C2,
	0x4BE, 0x4BB, 0x4B8, 0x4B5, 0x4B1, 0x4AE, 0x4AB, 0x4A8, 0x4A4, 0x4A1, 0x49E, 0x49B, 0x498, 0x494, 0x491, 0x48E,
	0x48B, 0x488, 0x485, 0x482, 0x47E, 0x47B, 0x478, 0x475, 0x472, 0x46F, 0x46C, 0x469, 0x466, 0x463, 0x460, 0x45D,
	0x45A, 0x457, 0x454, 0x451, 0x44E, 0x44B, 0x448, 0x445, 0x442, 0x43F, 0x43C, 0x439, 0x436, 0x433, 0x430, 0x42D,
	0x42A, 0x428, 0x425, 0x422, 0x41F, 0x41C, 0x419, 0x416, 0x414, 0x411, 0x40E, 0x40B, 0x408, 0x406, 0x403, 0x400
};

#if 0
void FM_Operator_Constant_Initialise(void)
{
	const cc_u16f sine_table_length = CC_COUNT_OF(logarithmic_attenuation_sine_table);
	const cc_u16f pow_table_length = CC_COUNT_OF(power_table);
	const double log2 = log(2.0);

	cc_u16f i;

	/* Generate sine wave lookup table. */
	for (i = 0; i < sine_table_length; ++i)
	{
		/* "Calculate the normalized phase value for the input into the sine table.Note
		    that this is calculated as a normalized result from 0.0-1.0 where 0 is not
		    reached, because the phase is calculated as if it was a 9-bit index with the
		    LSB fixed to 1. This was done so that the sine table would be more accurate
		    when it was "mirrored" to create the negative oscillation of the wave. It's
		    also convenient we don't have to worry about a phase of 0, because 0 is an
		    invalid input for a log function, which we need to use below." */
		const double phase_normalised = (double)((i << 1) + 1) / (double)(sine_table_length << 1);

		/* "Calculate the pure sine value for the input. Note that we only build a sine
		    table for a quarter of the full oscillation (0-PI/2), since the upper two bits
		    of the full phase are extracted by the external circuit." */
		const double sin_result_normalized = sin(phase_normalised * (CC_PI / 2.0));

		/* "Convert the sine result from a linear representation of volume, to a
		    logarithmic representation of attenuation. The YM2612 stores values in the sine
		    table in this form because logarithms simplify multiplication down to addition,
		    and this allowed them to attenuate the sine result by the envelope generator
		    output simply by adding the two numbers together." */
		const double sin_result_as_attenuation = -log(sin_result_normalized) / log2;
		/* "The division by log(2) is required because the log function is base 10, but the
		    YM2612 uses a base 2 logarithmic value. Dividing the base 10 log result by
		    log10(2) will convert the result to a base 2 logarithmic value, which can then
		    be converted back to a linear value by a pow2 function. In other words:
		    2^(log10(x)/log10(2)) = 2^log2(x) = x
		    If there was a native log2() function provided we could use that instead." */

		/* "Convert the attenuation value to a rounded 12-bit result in 4.8 fixed point
		    format." */
		const cc_u16l sinResult = (cc_u16l)((sin_result_as_attenuation * 256.0) + 0.5);

		/* "Write the result to the table." */
		logarithmic_attenuation_sine_table[i] = sinResult;
	}

	/* Generate power lookup table. */
	for (i = 0; i < pow_table_length; ++i)
	{
		/* "Normalize the current index to the range 0.0 - 1.0. Note that in this case, 0.0
		    is a value which is never actually reached, since we start from i+1. They only
		    did this to keep the result to an 11-bit output. It probably would have been
		    better to simply subtract 1 from every final number and have 1.0 as the input
		    limit instead when building the table, so an input of 0 would output 0x7FF,
		    but they didn't." */
		const double entry_normalised = (double)(i + 1) / (double)pow_table_length;

		/* "Calculate 2 ^ -entryNormalized." */
		const double result_normalised = pow(2.0, -entry_normalised);

		/* "Convert the normalized result to an 11-bit rounded result." */
		const cc_u16l result = (cc_u16l)((result_normalised * 2048.0) + 0.5);

		/* "Write the result to the table." */
		power_table[i] = result;
	}
}
#endif

static cc_u16f GetSSGEGCorrectedAttenuation(const FM_Operator* const state, const cc_bool disable_inversion)
{
	if (!disable_inversion && state->ssgeg.enabled && state->ssgeg.invert != state->ssgeg.attack)
		return (0x200 - state->attenuation) & 0x3FF;
	else
		return state->attenuation;
}

static cc_u16f CalculateRate(const FM_Operator* const state)
{
	if (state->rates[state->envelope_mode] == 0)
		return 0;

	return CC_MIN(0x3F, state->rates[state->envelope_mode] * 2 + (FM_Phase_GetKeyCode(&state->phase) >> state->key_scale));
}

static void EnterAttackMode(FM_Operator* const state)
{
	if (state->key_on)
	{
		state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_ATTACK;

		if (CalculateRate(state) >= 0x1F * 2)
		{
			state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_DECAY;
			state->attenuation = 0;
		}
	}
}

static cc_u16f InversePow2(const cc_u16f value)
{
	/* TODO: Maybe replace this whole thing with a single lookup table? */

	/* The attenuation is in 5.8 fixed point format. */
	const cc_u16f whole = value >> 8;
	const cc_u16f fraction = value & 0xFF;

	return (power_table[fraction] << 2) >> whole;
}

void FM_Operator_Initialise(FM_Operator* const state)
{
	FM_Phase_Initialise(&state->phase);

	/* Set envelope to update immediately. */
	state->countdown = 1;

	state->cycle_counter = 0;

	state->delta_index = 0;
	state->attenuation = 0x3FF;

	FM_Operator_SetSSGEG(state, 0);
	FM_Operator_SetTotalLevel(state, 0x7F); /* Silence channel. */
	FM_Operator_SetKeyScaleAndAttackRate(state, 0, 0);
	FM_Operator_SetDecayRate(state, 0);
	FM_Operator_SetSustainRate(state, 0);
	FM_Operator_SetSustainLevelAndReleaseRate(state, 0, 0);
	FM_Operator_SetAmplitudeModulationOn(state, cc_false);

	state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_RELEASE;

	state->key_on = cc_false;
}

void FM_Operator_SetKeyOn(FM_Operator* const state, const cc_bool key_on)
{
	/* An envelope cannot be key-on'd if it isn't key-off'd, and vice versa. */
	/* This is relied upon by Sonic's spring sound. */
	/* TODO: http://gendev.spritesmind.net/forum/viewtopic.php?p=6179#p6179 */
	/* Key-on/key-off operations should not occur until an envelope generator update. */
	if (state->key_on != key_on)
	{
		state->key_on = key_on;

		if (key_on)
		{
			EnterAttackMode(state);
			FM_Phase_Reset(&state->phase);
		}
		else
		{
			state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_RELEASE;

			/* SSG-EG attenuation inversion is not performed during key-off, so we have to manually invert the attenuation here. */
			state->attenuation = GetSSGEGCorrectedAttenuation(state, cc_false);

			/* This is always forced off as long as key-on is false. */
			state->ssgeg.invert = cc_false;
		}
	}
}

void FM_Operator_SetSSGEG(FM_Operator* const state, const cc_u8f ssgeg)
{
	state->ssgeg.enabled   = (ssgeg & (1u << 3)) != 0;
	state->ssgeg.attack    = (ssgeg & (1u << 2)) != 0 && state->ssgeg.enabled;
	state->ssgeg.alternate = (ssgeg & (1u << 1)) != 0 && state->ssgeg.enabled;
	state->ssgeg.hold      = (ssgeg & (1u << 0)) != 0 && state->ssgeg.enabled;
}

void FM_Operator_SetTotalLevel(FM_Operator* const state, const cc_u16f total_level)
{
	/* Convert from 7-bit to 10-bit. */
	state->total_level = total_level << 3;
}

void FM_Operator_SetKeyScaleAndAttackRate(FM_Operator* const state, const cc_u16f key_scale, const cc_u16f attack_rate)
{
	state->key_scale = 3 - key_scale;
	state->rates[FM_OPERATOR_ENVELOPE_MODE_ATTACK] = attack_rate;
}

void FM_Operator_SetSustainLevelAndReleaseRate(FM_Operator* const state, const cc_u16f sustain_level, const cc_u16f release_rate)
{
	state->sustain_level = sustain_level == 0xF ? 0x3E0 : sustain_level * 0x20;

	/* Convert from 4-bit to 5-bit to match the others. */
	state->rates[FM_OPERATOR_ENVELOPE_MODE_RELEASE] = (release_rate << 1) | 1;
}

#define FM_SATURATION_SUBTRACT(VALUE, SUBTRAHEND) (CC_MAX(VALUE, SUBTRAHEND) - SUBTRAHEND)

static cc_u16f GetEnvelopeDelta(FM_Operator* const state)
{
	if (--state->countdown == 0)
	{
		const cc_u16f rate = CalculateRate(state);

		state->countdown = 3;

		if ((state->cycle_counter++ & ((1 << FM_SATURATION_SUBTRACT(11, rate / 4)) - 1)) == 0)
		{
			static const cc_u16f deltas[0x40][8] = {
				{0, 0, 0, 0, 0, 0, 0, 0},
				{0, 0, 0, 0, 0, 0, 0, 0},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{0, 1, 0, 1, 0, 1, 0, 1},
				{0, 1, 0, 1, 1, 1, 0, 1},
				{0, 1, 1, 1, 0, 1, 1, 1},
				{0, 1, 1, 1, 1, 1, 1, 1},
				{1, 1, 1, 1, 1, 1, 1, 1},
				{1, 1, 1, 2, 1, 1, 1, 2},
				{1, 2, 1, 2, 1, 2, 1, 2},
				{1, 2, 2, 2, 1, 2, 2, 2},
				{2, 2, 2, 2, 2, 2, 2, 2},
				{2, 2, 2, 3, 2, 2, 2, 3},
				{2, 3, 2, 3, 2, 3, 2, 3},
				{2, 3, 3, 3, 2, 3, 3, 3},
				{3, 3, 3, 3, 3, 3, 3, 3},
				{3, 3, 3, 4, 3, 3, 3, 4},
				{3, 4, 3, 4, 3, 4, 3, 4},
				{3, 4, 4, 4, 3, 4, 4, 4},
				{4, 4, 4, 4, 4, 4, 4, 4},
				{4, 4, 4, 4, 4, 4, 4, 4},
				{4, 4, 4, 4, 4, 4, 4, 4},
				{4, 4, 4, 4, 4, 4, 4, 4}
			};

			return deltas[rate][state->delta_index++ % CC_COUNT_OF(deltas[rate])];
		}
	}

	return 0;
}

static void UpdateEnvelopeSSGEG(FM_Operator* const state)
{
	if (state->ssgeg.enabled && state->attenuation >= 0x200)
	{
		if (state->ssgeg.alternate)
			state->ssgeg.invert = state->ssgeg.hold ? cc_true : !state->ssgeg.invert;
		else if (!state->ssgeg.hold)
			FM_Phase_Reset(&state->phase);

		if (!state->ssgeg.hold)
			EnterAttackMode(state);
	}
}

static void UpdateEnvelopeADSR(FM_Operator* const state)
{
	const cc_u16f delta = GetEnvelopeDelta(state);
	const cc_bool end_envelope = state->attenuation >= (state->ssgeg.enabled ? 0x200 : 0x3F0);

	switch (state->envelope_mode)
	{
		case FM_OPERATOR_ENVELOPE_MODE_ATTACK:
			if (state->attenuation == 0)
			{
				state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_DECAY;
				break;
			}

			if (delta != 0)
			{
				state->attenuation += (~(cc_u16f)state->attenuation << (delta - 1)) >> 4;
				assert(state->attenuation <= 0x3FF);
			}

			break;

		case FM_OPERATOR_ENVELOPE_MODE_DECAY:
			if (state->attenuation >= state->sustain_level)
			{
				state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_SUSTAIN;
				break;
			}
			/* Fallthrough */
		case FM_OPERATOR_ENVELOPE_MODE_SUSTAIN:
		case FM_OPERATOR_ENVELOPE_MODE_RELEASE:
			if (delta != 0 && !end_envelope)
			{
				state->attenuation += 1u << ((delta - 1) + (state->ssgeg.enabled ? 2 : 0));
				assert(state->attenuation <= 0x3FF);
			}

			if (end_envelope && !(state->key_on && state->ssgeg.hold && (state->ssgeg.alternate != state->ssgeg.attack)))
			{
				state->envelope_mode = FM_OPERATOR_ENVELOPE_MODE_RELEASE;
				state->attenuation = 0x3FF;
			}

			break;
	}
}

static cc_u16f GetEnvelopeAttenuation(FM_Operator* const state, const cc_u8f amplitude_modulation, const cc_u8f amplitude_modulation_shift)
{
	const cc_u8f final_amplitude_modulation = state->amplitude_modulation_on ? amplitude_modulation >> amplitude_modulation_shift : 0;
	const cc_u16f attenuation = GetSSGEGCorrectedAttenuation(state, !state->key_on) + final_amplitude_modulation + state->total_level;

	/* TODO: TL isn't added here if this is FM3 and CSM is enabled! */
	return CC_MIN(0x3FF, attenuation);
}

static cc_u16f UpdateEnvelope(FM_Operator* const state, const cc_u8f amplitude_modulation, const cc_u8f amplitude_modulation_shift)
{
	UpdateEnvelopeSSGEG(state);
	UpdateEnvelopeADSR(state);

	return GetEnvelopeAttenuation(state, amplitude_modulation, amplitude_modulation_shift);
}

cc_u16f FM_Operator_Process(FM_Operator* const state, const cc_u8f amplitude_modulation, const cc_u8f amplitude_modulation_shift, const cc_u16f phase_modulation)
{
	/* Update and obtain phase and make it 10-bit (the upper bits are discarded later). */
	const cc_u16f phase = FM_Phase_Increment(&state->phase) >> 10;

	/* Update and obtain attenuation (10-bit). */
	const cc_u16f attenuation = UpdateEnvelope(state, amplitude_modulation, amplitude_modulation_shift);

	/* Modulate the phase. */
	/* The modulation is divided by two because up to two operators can provide modulation at once. */
	const cc_u16f modulated_phase = (phase + phase_modulation / 2) & 0x3FF;

	/* Reduce the phase down to a single quarter of the span of a sine wave, since the other three quarters
	   are just mirrored anyway. This allows us to use a much smaller sine wave lookup table. */
	const cc_bool phase_is_in_negative_wave = (modulated_phase & 0x200) != 0;
	const cc_bool phase_is_in_mirrored_half_of_wave = (modulated_phase & 0x100) != 0;
	const cc_u16f quarter_phase = (modulated_phase & 0xFF) ^ (phase_is_in_mirrored_half_of_wave ? 0xFF : 0);

	/* This table triples as a sine wave lookup table, a logarithm lookup table, and an attenuation lookup table.
	   The obtained attenuation is 12-bit. */
	const cc_u16f phase_as_attenuation = logarithmic_attenuation_sine_table[quarter_phase];

	/* Both attenuations are logarithms (measurements of decibels), so we can attenuate them by each other by just adding
	   them together instead of multiplying them. The result is a 13-bit value. */
	const cc_u16f combined_attenuation = phase_as_attenuation + (attenuation << 2);

	/* Convert from logarithm (decibel) back to linear (sound pressure). */
	const cc_u16f sample_absolute = InversePow2(combined_attenuation);

	/* Restore the sign bit that we extracted earlier. */
	const cc_u16f sample = phase_is_in_negative_wave ? 0 - sample_absolute : sample_absolute;

	/* Return the sign-extended 14-bit sample. */
	return sample;
}
