#include "fm-channel.h"

#include <assert.h>

#include "clowncommon/clowncommon.h"

static cc_u8f ComputeFeedbackDivisor(const cc_u8f value)
{
	assert(value <= 9);
	return 9 - value;
}

static void SetAmplitudeModulation(FM_Channel* const state, const cc_u8f amplitude_modulation)
{
	state->amplitude_modulation_shift = 7 >> amplitude_modulation;
}

void FM_Channel_Initialise(FM_Channel* const state)
{
	cc_u16f i;

	for (i = 0; i < CC_COUNT_OF(state->operators); ++i)
		FM_Operator_Initialise(&state->operators[i]);

	state->feedback_divisor = ComputeFeedbackDivisor(0);
	state->algorithm = 0;

	for (i = 0; i < CC_COUNT_OF(state->operator_1_previous_samples); ++i)
		state->operator_1_previous_samples[i] = 0;

	SetAmplitudeModulation(state, 0);
	state->phase_modulation_sensitivity = 0;
}

void FM_Channel_SetFrequencies(FM_Channel* const channel, const cc_u8f modulation, const cc_u16f f_number_and_block)
{
	cc_u16f i;

	for (i = 0; i < CC_COUNT_OF(channel->operators); ++i)
		FM_Operator_SetFrequency(&channel->operators[i], modulation, channel->phase_modulation_sensitivity, f_number_and_block);
}

void FM_Channel_SetFeedbackAndAlgorithm(FM_Channel* const channel, const cc_u8f feedback, const cc_u8f algorithm)
{
	channel->feedback_divisor = ComputeFeedbackDivisor(feedback);
	channel->algorithm = algorithm;
}

static void FM_Channel_SetPhaseModulationAndSensitivity(FM_Channel* const channel, const cc_u8f phase_modulation, const cc_u8f phase_modulation_sensitivity)
{
	cc_u16f i;

	for (i = 0; i < CC_COUNT_OF(channel->operators); ++i)
		FM_Operator_SetPhaseModulationAndSensitivity(&channel->operators[i], phase_modulation, phase_modulation_sensitivity);
}

void FM_Channel_SetModulationSensitivity(FM_Channel* const channel, const cc_u8f phase_modulation, const cc_u8f amplitude, const cc_u8f phase)
{
	SetAmplitudeModulation(channel, amplitude);
	channel->phase_modulation_sensitivity = phase;

	FM_Channel_SetPhaseModulationAndSensitivity(channel, phase_modulation, phase);
}

void FM_Channel_SetPhaseModulation(FM_Channel* const channel, const cc_u8f phase_modulation)
{
	FM_Channel_SetPhaseModulationAndSensitivity(channel, phase_modulation, channel->phase_modulation_sensitivity);
}

static cc_u16f FM_Channel_Signed14BitToUnsigned9Bit(const cc_u16f value)
{
	return (value + (1 << (14 - 1))) >> (14 - 9);
}

/* Wacky logic to efficiently perform a signed clamp on an unsigned value. */
#define FM_CLAMP_SIGNED_ON_UNSIGNED(BITS, MIN, MAX, VALUE) \
	(CC_CLAMP((1 << ((BITS) - 1)) + (MIN), (1 << ((BITS) - 1)) + (MAX), (VALUE)) - (1 << ((BITS) - 2)))

static cc_u16f FM_Channel_MixSamples(const cc_u16f a, const cc_u16f b)
{
	return FM_CLAMP_SIGNED_ON_UNSIGNED(9 + 1, -0x100, 0xFF, a + b);
}

cc_u16f FM_Channel_GetSample(FM_Channel* const channel, const cc_u8f amplitude_modulation)
{
	const cc_u8f amplitude_modulation_shift = channel->amplitude_modulation_shift;

	FM_Operator* const operator1 = &channel->operators[0];
	FM_Operator* const operator2 = &channel->operators[1];
	FM_Operator* const operator3 = &channel->operators[2];
	FM_Operator* const operator4 = &channel->operators[3];

	cc_u16f feedback_modulation;
	cc_u16f operator_1_sample;
	cc_u16f operator_2_sample;
	cc_u16f operator_3_sample;
	cc_u16f operator_4_sample;
	cc_u16f sample;

	/* Compute operator 1's self-feedback modulation. */
	if (channel->feedback_divisor == ComputeFeedbackDivisor(0))
	{
		feedback_modulation = 0;
	}
	else
	{
		feedback_modulation = (channel->operator_1_previous_samples[0] + channel->operator_1_previous_samples[1]) >> channel->feedback_divisor;
		feedback_modulation = CC_SIGN_EXTEND(cc_u16f, 15 - channel->feedback_divisor, feedback_modulation);
	}

	/* Feed the operators into each other to produce the final sample. */
	/* Note that the operators output a 14-bit sample, meaning that, if all four are summed, then the result is a 16-bit sample,
	   so there is no possibility of overflow. */
	/* http://gendev.spritesmind.net/forum/viewtopic.php?p=5958#p5958 */
	switch (channel->algorithm)
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

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample);

			break;

		case 1:
			/* "Three double modulation serial connection mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_1_sample + operator_2_sample);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_3_sample);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample);

			break;

		case 2:
			/* "Double modulation mode (1)". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);

			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, 0);
			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_2_sample);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_1_sample + operator_3_sample);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample);

			break;

		case 3:
			/* "Double modulation mode (2)". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_2_sample + operator_3_sample);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample);

			break;

		case 4:
			/* "Two serial connection and two parallel modes". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_3_sample);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_2_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample));

			break;

		case 5:
			/* "Common modulation 3 parallel mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);

			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);
			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);
			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_2_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_3_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample));

			break;

		case 6:
			/* "Two serial connection + two sine mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);
			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, operator_1_sample);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, 0);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_2_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_3_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample));

			break;

		case 7:
			/* "Four parallel sine synthesis mode". */
			operator_1_sample = FM_Operator_Process(operator1, amplitude_modulation, amplitude_modulation_shift, feedback_modulation);

			operator_2_sample = FM_Operator_Process(operator2, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_3_sample = FM_Operator_Process(operator3, amplitude_modulation, amplitude_modulation_shift, 0);

			operator_4_sample = FM_Operator_Process(operator4, amplitude_modulation, amplitude_modulation_shift, 0);

			sample = FM_Channel_Signed14BitToUnsigned9Bit(operator_1_sample);
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_2_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_3_sample));
			sample = FM_Channel_MixSamples(sample, FM_Channel_Signed14BitToUnsigned9Bit(operator_4_sample));

			break;
	}

	/* Update the feedback values. */
	channel->operator_1_previous_samples[1] = channel->operator_1_previous_samples[0];
	channel->operator_1_previous_samples[0] = operator_1_sample;

	return sample;
}
