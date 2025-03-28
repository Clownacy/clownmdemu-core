#include "fm-channel.h"

#include <assert.h>

#include "clowncommon/clowncommon.h"

static cc_s16f ComputeFeedbackDivisor(const cc_u16f value)
{
	assert(value <= 9);
	return 1 << (9 - value);
}

static void SetAmplitudeModulation(FM_Channel_State* const state, const cc_u8f amplitude_modulation)
{
	state->amplitude_modulation_shift = 7 >> amplitude_modulation;
}

void FM_Channel_State_Initialise(FM_Channel_State* const state)
{
	cc_u16f i;

	for (i = 0; i < CC_COUNT_OF(state->operators); ++i)
		FM_Operator_State_Initialise(&state->operators[i]);

	state->feedback_divisor = ComputeFeedbackDivisor(0);
	state->algorithm = 0;

	for (i = 0; i < CC_COUNT_OF(state->operator_1_previous_samples); ++i)
		state->operator_1_previous_samples[i] = 0;

	SetAmplitudeModulation(state, 0);
	state->phase_modulation_sensitivity = 0;
}

void FM_Channel_Parameters_Initialise(FM_Channel* const channel, const FM_Channel_Constant* const constant, FM_Channel_State* const state)
{
	cc_u16f i;

	channel->constant = constant;
	channel->state = state;

	for (i = 0; i < CC_COUNT_OF(channel->operators); ++i)
	{
		channel->operators[i].constant = &constant->operators;
		channel->operators[i].state = &state->operators[i];
	}
}

void FM_Channel_SetFrequencies(const FM_Channel* const channel, const cc_u8f modulation, const cc_u16f f_number_and_block)
{
	cc_u16f i;

	for (i = 0; i < CC_COUNT_OF(channel->state->operators); ++i)
		FM_Operator_SetFrequency(channel->operators[i].state, modulation, channel->state->phase_modulation_sensitivity, f_number_and_block);
}

void FM_Channel_SetFeedbackAndAlgorithm(const FM_Channel* const channel, const cc_u16f feedback, const cc_u16f algorithm)
{
	channel->state->feedback_divisor = ComputeFeedbackDivisor(feedback);
	channel->state->algorithm = algorithm;
}

static void FM_Channel_SetPhaseModulationAndSensitivity(const FM_Channel* const channel, const cc_u8f phase_modulation, const cc_u8f phase_modulation_sensitivity)
{
	cc_u16f i;

	for (i = 0; i < CC_COUNT_OF(channel->state->operators); ++i)
		FM_Operator_SetPhaseModulationAndSensitivity(channel->operators[i].state, phase_modulation, phase_modulation_sensitivity);
}

void FM_Channel_SetModulationSensitivity(const FM_Channel* const channel, const cc_u8f phase_modulation, const cc_u8f amplitude, const cc_u8f phase)
{
	SetAmplitudeModulation(channel->state, amplitude);
	channel->state->phase_modulation_sensitivity = phase;

	FM_Channel_SetPhaseModulationAndSensitivity(channel, phase_modulation, phase);
}

void FM_Channel_SetPhaseModulation(const FM_Channel* const channel, const cc_u8f phase_modulation)
{
	FM_Channel_SetPhaseModulationAndSensitivity(channel, phase_modulation, channel->state->phase_modulation_sensitivity);
}

/* Portable equivalent to bit-shifting. */
/* TODO: Instead, maybe convert all signed integers to unsigned so that we can implement
   two's-compliment manually which would allow us to be able to perform simple bit-shifting instead. */
static cc_s16f FM_Channel_DiscardLowerBits(const cc_s16f total_bits_to_discard, const cc_s16f value)
{
	const cc_s16f divisor = 1 << total_bits_to_discard;
	return (value - (value < 0 ? divisor - 1 : 0)) / divisor;
}

static cc_s16f FM_Channel_14BitTo9Bit(const cc_s16f value)
{
	return FM_Channel_DiscardLowerBits(14 - 9, value);
}

static cc_s16f FM_Channel_MixSamples(const cc_s16f a, const cc_s16f b)
{
	return CC_CLAMP(-0x100, 0xFF, a + b);
}

cc_s16f FM_Channel_GetSample(const FM_Channel* const channel, const cc_u8f amplitude_modulation)
{
	FM_Channel_State* const state = channel->state;
	const cc_u8f amplitude_modulation_shift = state->amplitude_modulation_shift;

	const FM_Operator* const operator1 = &channel->operators[0];
	const FM_Operator* const operator2 = &channel->operators[1];
	const FM_Operator* const operator3 = &channel->operators[2];
	const FM_Operator* const operator4 = &channel->operators[3];

	cc_s16f feedback_modulation;
	cc_s16f operator_1_sample;
	cc_s16f operator_2_sample;
	cc_s16f operator_3_sample;
	cc_s16f operator_4_sample;
	cc_s16f sample;

	/* Compute operator 1's self-feedback modulation. */
	if (state->feedback_divisor == ComputeFeedbackDivisor(0))
		feedback_modulation = 0;
	else
		feedback_modulation = (state->operator_1_previous_samples[0] + state->operator_1_previous_samples[1]) / state->feedback_divisor;

	/* Feed the operators into each other to produce the final sample. */
	/* Note that the operators output a 14-bit sample, meaning that, if all four are summed, then the result is a 16-bit sample,
	   so there is no possibility of overflow. */
	/* http://gendev.spritesmind.net/forum/viewtopic.php?p=5958#p5958 */
	switch (state->algorithm)
	{
		default:
			/* Should not happen. */
			assert(0);
			/* Fallthrough */
		case 0:
			/* "Four serial connection mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);
			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_2_sample);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_3_sample);

			sample = FM_Channel_14BitTo9Bit(operator_4_sample);

			break;

		case 1:
			/* "Three double modulation serial connection mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_1_sample + operator_2_sample);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_3_sample);

			sample = FM_Channel_14BitTo9Bit(operator_4_sample);

			break;

		case 2:
			/* "Double modulation mode (1)". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);

			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, 0);
			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_2_sample);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_1_sample + operator_3_sample);

			sample = FM_Channel_14BitTo9Bit(operator_4_sample);

			break;

		case 3:
			/* "Double modulation mode (2)". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_2_sample + operator_3_sample);

			sample = FM_Channel_14BitTo9Bit(operator_4_sample);

			break;

		case 4:
			/* "Two serial connection and two parallel modes". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_3_sample);

			sample = FM_Channel_14BitTo9Bit(operator_2_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_4_sample));

			break;

		case 5:
			/* "Common modulation 3 parallel mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);

			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);
			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			sample = FM_Channel_14BitTo9Bit(operator_2_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_3_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_4_sample));

			break;

		case 6:
			/* "Two serial connection + two sine mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, 0);

			sample = FM_Channel_14BitTo9Bit(operator_2_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_3_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_4_sample));

			break;

		case 7:
			/* "Four parallel sine synthesis mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);

			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, 0);

			sample = FM_Channel_14BitTo9Bit(operator_1_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_2_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_3_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_14BitTo9Bit(operator_4_sample));

			break;
	}

	/* Update the feedback values. */
	state->operator_1_previous_samples[1] = state->operator_1_previous_samples[0];
	state->operator_1_previous_samples[0] = operator_1_sample;

	return sample;
}
