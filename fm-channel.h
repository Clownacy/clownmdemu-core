#ifndef FM_CHANNEL_H
#define FM_CHANNEL_H

#include "clowncommon/clowncommon.h"

#include "fm-operator.h"

#define FM_TOTAL_OPERATORS 4

#define FM_CHANNEL_PARAMETERS_INITIALISE(CONSTANT, STATE) { \
		(CONSTANT), \
		(STATE), \
\
		{ \
			{ \
				&(CONSTANT)->operators, \
				&(STATE)->operators[0] \
			}, \
\
			{ \
				&(CONSTANT)->operators, \
				&(STATE)->operators[1] \
			}, \
\
			{ \
				&(CONSTANT)->operators, \
				&(STATE)->operators[2] \
			}, \
\
			{ \
				&(CONSTANT)->operators, \
				&(STATE)->operators[3] \
			} \
		} \
	}

typedef struct FM_Channel_Constant
{
	FM_Operator_Constant operators;
} FM_Channel_Constant;

typedef struct FM_Channel_State
{
	FM_Operator_State operators[FM_TOTAL_OPERATORS];
	cc_u8l feedback_divisor;
	cc_u16l algorithm;
	cc_u16l operator_1_previous_samples[2];
	cc_u8l amplitude_modulation_shift, phase_modulation_sensitivity;
} FM_Channel_State;

typedef struct FM_Channel
{
	const FM_Channel_Constant *constant;
	FM_Channel_State *state;

	FM_Operator operators[FM_TOTAL_OPERATORS];
} FM_Channel;

#define FM_Channel_Constant_Initialise(constant) FM_Operator_Constant_Initialise(&(constant)->operators)
void FM_Channel_State_Initialise(FM_Channel_State *state);
void FM_Channel_Parameters_Initialise(FM_Channel *channel, const FM_Channel_Constant *constant, FM_Channel_State *state);

/* Per-channel. */
#define FM_Channel_SetFrequency(channel, operator_index, modulation, f_number_and_block) FM_Operator_SetFrequency((channel)->operators[operator_index].state, modulation, (channel)->state->phase_modulation_sensitivity, f_number_and_block)
void FM_Channel_SetFrequencies(const FM_Channel *channel, cc_u8f modulation, cc_u16f f_number_and_block);
void FM_Channel_SetFeedbackAndAlgorithm(const FM_Channel *channel, cc_u8f feedback, cc_u8f algorithm);
void FM_Channel_SetModulationSensitivity(const FM_Channel *channel, cc_u8f phase_modulation, cc_u8f amplitude, cc_u8f phase);
void FM_Channel_SetPhaseModulation(const FM_Channel *channel, cc_u8f phase_modulation);

/* Per-operator. */
#define FM_Channel_SetKeyOn(channel, operator_index, key_on) FM_Operator_SetKeyOn((channel)->operators[operator_index].state, key_on)
#define FM_Channel_SetDetuneAndMultiplier(channel, operator_index, modulation, detune, multiplier) FM_Operator_SetDetuneAndMultiplier((channel)->operators[operator_index].state, modulation, (channel)->state->phase_modulation_sensitivity, detune, multiplier)
#define FM_Channel_SetTotalLevel(channel, operator_index, total_level) FM_Operator_SetTotalLevel((channel)->operators[operator_index].state, total_level)
#define FM_Channel_SetKeyScaleAndAttackRate(channel, operator_index, key_scale, attack_rate) FM_Operator_SetKeyScaleAndAttackRate((channel)->operators[operator_index].state, key_scale, attack_rate)
#define FM_Channel_SetDecayRate(channel, operator_index, decay_rate) FM_Operator_SetDecayRate((channel)->operators[operator_index].state, decay_rate)
#define FM_Channel_SetSustainRate(channel, operator_index, sustain_rate) FM_Operator_SetSustainRate((channel)->operators[operator_index].state, sustain_rate)
#define FM_Channel_SetSustainLevelAndReleaseRate(channel, operator_index, sustain_level, release_rate) FM_Operator_SetSustainLevelAndReleaseRate((channel)->operators[operator_index].state, sustain_level, release_rate)
#define FM_Channel_SetSSGEG(channel, operator_index, ssgeg) FM_Operator_SetSSGEG((channel)->operators[operator_index].state, ssgeg)
#define FM_Channel_SetAmplitudeModulationOn(channel, operator_index, amplitude_modulation_on) FM_Operator_SetAmplitudeModulationOn((channel)->operators[operator_index].state, amplitude_modulation_on)

cc_u16f FM_Channel_GetSample(const FM_Channel *channel, cc_u8f amplitude_modulation);

#endif /* FM_CHANNEL_H */
