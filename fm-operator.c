/* Based on Nemesis's notes: http://gendev.spritesmind.net/forum/viewtopic.php?p=6114#p6114 */

/* http://gendev.spritesmind.net/forum/viewtopic.php?p=5716#p5716 */
/* http://gendev.spritesmind.net/forum/viewtopic.php?p=7967#p7967 */

#include "fm-operator.h"

#include <assert.h>
#include <math.h>

#include "clowncommon/clowncommon.h"

static cc_u16f GetSSGEGCorrectedAttenuation(const FM_Operator_State* const state, const cc_bool disable_inversion)
{
	if (!disable_inversion && state->ssgeg.enabled && state->ssgeg.invert != state->ssgeg.attack)
		return (0x200 - state->attenuation) & 0x3FF;
	else
		return state->attenuation;
}

static cc_u16f CalculateRate(const FM_Operator_State* const state)
{
	if (state->rates[state->envelope_mode] == 0)
		return 0;

	return CC_MIN(0x3F, state->rates[state->envelope_mode] * 2 + (FM_Phase_GetKeyCode(&state->phase) / state->key_scale));
}

static void EnterAttackMode(FM_Operator_State* const state)
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

static cc_u16f InversePow2(const FM_Operator_Constant* const constant, const cc_u16f value)
{
	/* TODO: Maybe replace this whole thing with a single lookup table? */

	/* The attenuation is in 5.8 fixed point format. */
	const cc_u16f whole = value >> 8;
	const cc_u16f fraction = value & 0xFF;

	return (constant->power_table[fraction] << 2) >> whole;
}

void FM_Operator_Constant_Initialise(FM_Operator_Constant* const constant)
{
	const cc_u16f sine_table_length = CC_COUNT_OF(constant->logarithmic_attenuation_sine_table);
	const cc_u16f pow_table_length = CC_COUNT_OF(constant->power_table);
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
		constant->logarithmic_attenuation_sine_table[i] = sinResult;
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
		constant->power_table[i] = result;
	}
}

void FM_Operator_State_Initialise(FM_Operator_State* const state)
{
	FM_Phase_State_Initialise(&state->phase);

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

void FM_Operator_SetKeyOn(FM_Operator_State* const state, const cc_bool key_on)
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

void FM_Operator_SetSSGEG(FM_Operator_State* const state, const cc_u8f ssgeg)
{
	state->ssgeg.enabled   = (ssgeg & (1u << 3)) != 0;
	state->ssgeg.attack    = (ssgeg & (1u << 2)) != 0 && state->ssgeg.enabled;
	state->ssgeg.alternate = (ssgeg & (1u << 1)) != 0 && state->ssgeg.enabled;
	state->ssgeg.hold      = (ssgeg & (1u << 0)) != 0 && state->ssgeg.enabled;
}

void FM_Operator_SetTotalLevel(FM_Operator_State* const state, const cc_u16f total_level)
{
	/* Convert from 7-bit to 10-bit. */
	state->total_level = total_level << 3;
}

void FM_Operator_SetKeyScaleAndAttackRate(FM_Operator_State* const state, const cc_u16f key_scale, const cc_u16f attack_rate)
{
	state->key_scale = 8 >> key_scale;
	state->rates[FM_OPERATOR_ENVELOPE_MODE_ATTACK] = attack_rate;
}

void FM_Operator_SetSustainLevelAndReleaseRate(FM_Operator_State* const state, const cc_u16f sustain_level, const cc_u16f release_rate)
{
	state->sustain_level = sustain_level == 0xF ? 0x3E0 : sustain_level * 0x20;

	/* Convert from 4-bit to 5-bit to match the others. */
	state->rates[FM_OPERATOR_ENVELOPE_MODE_RELEASE] = (release_rate << 1) | 1;
}

static cc_u16f GetEnvelopeDelta(FM_Operator_State* const state)
{
	if (--state->countdown == 0)
	{
		static const cc_u16f cycle_bitmasks[0x40 / 4] = {
			#define GENERATE_BITMASK(x) ((1 << (x)) - 1)
			GENERATE_BITMASK(11),
			GENERATE_BITMASK(10),
			GENERATE_BITMASK(9),
			GENERATE_BITMASK(8),
			GENERATE_BITMASK(7),
			GENERATE_BITMASK(6),
			GENERATE_BITMASK(5),
			GENERATE_BITMASK(4),
			GENERATE_BITMASK(3),
			GENERATE_BITMASK(2),
			GENERATE_BITMASK(1),
			GENERATE_BITMASK(0),
			GENERATE_BITMASK(0),
			GENERATE_BITMASK(0),
			GENERATE_BITMASK(0),
			GENERATE_BITMASK(0)
			#undef GENERATE_BITMASK
		};

		const cc_u16f rate = CalculateRate(state);

		state->countdown = 3;

		if ((state->cycle_counter++ & cycle_bitmasks[rate / 4]) == 0)
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

static void UpdateEnvelopeSSGEG(FM_Operator_State* const state)
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

static void UpdateEnvelopeADSR(FM_Operator_State* const state)
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

static cc_u16f GetEnvelopeAttenuation(FM_Operator_State* const state, const cc_u8f amplitude_modulation, const cc_u8f amplitude_modulation_shift)
{
	const cc_u8f final_amplitude_modulation = state->amplitude_modulation_on ? amplitude_modulation >> amplitude_modulation_shift : 0;
	const cc_u16f attenuation = GetSSGEGCorrectedAttenuation(state, !state->key_on) + final_amplitude_modulation + state->total_level;

	/* TODO: TL isn't added here if this is FM3 and CSM is enabled! */
	return CC_MIN(0x3FF, attenuation);
}

static cc_u16f UpdateEnvelope(FM_Operator_State* const state, const cc_u8f amplitude_modulation, const cc_u8f amplitude_modulation_shift)
{
	UpdateEnvelopeSSGEG(state);
	UpdateEnvelopeADSR(state);

	return GetEnvelopeAttenuation(state, amplitude_modulation, amplitude_modulation_shift);
}

cc_u16f FM_Operator_Process(const FM_Operator* const fm_operator, const cc_u8f amplitude_modulation, const cc_u8f amplitude_modulation_shift, const cc_u16f phase_modulation)
{
	/* Update and obtain phase and make it 10-bit (the upper bits are discarded later). */
	const cc_u16f phase = FM_Phase_Increment(&fm_operator->state->phase) >> 10;

	/* Update and obtain attenuation (10-bit). */
	const cc_u16f attenuation = UpdateEnvelope(fm_operator->state, amplitude_modulation, amplitude_modulation_shift);

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
	const cc_u16f phase_as_attenuation = fm_operator->constant->logarithmic_attenuation_sine_table[quarter_phase];

	/* Both attenuations are logarithms (measurements of decibels), so we can attenuate them by each other by just adding
	   them together instead of multiplying them. The result is a 13-bit value. */
	const cc_u16f combined_attenuation = phase_as_attenuation + (attenuation << 2);

	/* Convert from logarithm (decibel) back to linear (sound pressure). */
	const cc_u16f sample_absolute = InversePow2(fm_operator->constant, combined_attenuation);

	/* Restore the sign bit that we extracted earlier. */
	const cc_u16f sample = phase_is_in_negative_wave ? 0 - sample_absolute : sample_absolute;

	/* Return the sign-extended 14-bit sample. */
	return sample;
}
