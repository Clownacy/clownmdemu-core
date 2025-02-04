#ifndef FM_CHANNEL_H
#define FM_CHANNEL_H

#include "clowncommon/clowncommon.h"

#include "fm-operator.h"

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
	FM_Operator_State operators[4];
	cc_s16l feedback_divisor;
	cc_u16l algorithm;
	cc_s16l operator_1_previous_samples[2];
	cc_u8f ams, fms; /* TODO: Rename 'fms' to 'pms'. */
} FM_Channel_State;

typedef struct FM_Channel
{
	const FM_Channel_Constant *constant;
	FM_Channel_State *state;

	FM_Operator operators[4];
} FM_Channel;

void FM_Channel_Constant_Initialise(FM_Channel_Constant *constant);
void FM_Channel_State_Initialise(FM_Channel_State *state);
void FM_Channel_Parameters_Initialise(FM_Channel *channel, const FM_Channel_Constant *constant, FM_Channel_State *state);

/* Per-channel. */
void FM_Channel_SetFrequency(const FM_Channel *channel, cc_u16f f_number_and_block);
void FM_Channel_SetFrequencies(const FM_Channel *channel, const cc_u16l *f_number_and_block);
void FM_Channel_SetFeedbackAndAlgorithm(const FM_Channel *channel, cc_u16f feedback, cc_u16f algorithm);
void FM_Channel_SetSSGEG(const FM_Channel *channel, cc_u8f ssgeg);
void FM_Channel_SetAMSAndFMS(const FM_Channel *channel, cc_u8f ams, cc_u8f fms);

/* Per-operator. */
void FM_Channel_SetKeyOn(const FM_Channel *channel, cc_u16f operator_index, cc_bool key_on);
void FM_Channel_SetDetuneAndMultiplier(const FM_Channel *channel, cc_u16f operator_index, cc_u16f detune, cc_u16f multiplier);
void FM_Channel_SetTotalLevel(const FM_Channel *channel, cc_u16f operator_index, cc_u16f total_level);
void FM_Channel_SetKeyScaleAndAttackRate(const FM_Channel *channel, cc_u16f operator_index, cc_u16f key_scale, cc_u16f attack_rate);
void FM_Channel_SetDecayRate(const FM_Channel *channel, cc_u16f operator_index, cc_u16f decay_rate);
void FM_Channel_SetSustainRate(const FM_Channel *channel, cc_u16f operator_index, cc_u16f sustain_rate);
void FM_Channel_SetSustainLevelAndReleaseRate(const FM_Channel *channel, cc_u16f operator_index, cc_u16f sustain_level, cc_u16f release_rate);

cc_s16f FM_Channel_GetSample(const FM_Channel *channel);

#endif /* FM_CHANNEL_H */
