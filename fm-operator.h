#ifndef FM_OPERATOR_H
#define FM_OPERATOR_H

#include "clowncommon/clowncommon.h"

#include "fm-phase.h"

/* TODO: Rename. */
typedef enum FM_Operator_EnvelopeMode
{
	FM_OPERATOR_ENVELOPE_MODE_ATTACK = 0,
	FM_OPERATOR_ENVELOPE_MODE_DECAY = 1,
	FM_OPERATOR_ENVELOPE_MODE_SUSTAIN = 2,
	FM_OPERATOR_ENVELOPE_MODE_RELEASE = 3
} FM_Operator_EnvelopeMode;

typedef struct FM_Operator_Constant
{
	cc_u16l logarithmic_attenuation_sine_table[0x100];
	cc_u16l power_table[0x100];
} FM_Operator_Constant;

typedef struct FM_Operator_State
{
	FM_Phase_State phase;

	/* TODO: Make these two global. */
	cc_u16l countdown;
	cc_u16l cycle_counter;

	cc_u16l delta_index;
	cc_u16l attenuation;

	cc_u16l total_level;
	cc_u16l sustain_level;
	cc_u16l key_scale;

	cc_u16l rates[4];
	FM_Operator_EnvelopeMode envelope_mode;

	cc_bool key_on;
	cc_bool amplitude_modulation_on;

	/* TODO: Make this part of FM_Channel instead. */
	struct
	{
		cc_bool enabled;
		cc_bool attack;
		cc_bool alternate;
		cc_bool hold;
		cc_bool invert;
	} ssgeg;
} FM_Operator_State;

typedef struct FM_Operator
{
	const FM_Operator_Constant *constant;
	FM_Operator_State *state;
} FM_Operator;

void FM_Operator_Constant_Initialise(FM_Operator_Constant *constant);
void FM_Operator_State_Initialise(FM_Operator_State *state);

#define FM_Operator_SetFrequency(state, modulation, sensitivity, f_number_and_block) FM_Phase_SetFrequency(&(state)->phase, modulation, sensitivity, f_number_and_block)
void FM_Operator_SetKeyOn(FM_Operator_State *state, cc_bool key_on);
void FM_Operator_SetSSGEG(FM_Operator_State *state, cc_u8f ssgeg);
#define FM_Operator_SetDetuneAndMultiplier(state, modulation, sensitivity, detune, multiplier) FM_Phase_SetDetuneAndMultiplier(&(state)->phase, modulation, sensitivity, detune, multiplier)
void FM_Operator_SetTotalLevel(FM_Operator_State *state, cc_u16f total_level);
void FM_Operator_SetKeyScaleAndAttackRate(FM_Operator_State *state, cc_u16f key_scale, cc_u16f attack_rate);
#define FM_Operator_SetDecayRate(state, decay_rate) ((state)->rates[FM_OPERATOR_ENVELOPE_MODE_DECAY] = decay_rate)
#define FM_Operator_SetSustainRate(state, sustain_rate) ((state)->rates[FM_OPERATOR_ENVELOPE_MODE_SUSTAIN] = sustain_rate)
void FM_Operator_SetSustainLevelAndReleaseRate(FM_Operator_State *state, cc_u16f sustain_level, cc_u16f release_rate);
#define FM_Operator_SetAmplitudeModulationOn(state, amon) ((state)->amplitude_modulation_on = (amon))
#define FM_Operator_SetPhaseModulationAndSensitivity(state, modulation, sensitivity) FM_Phase_SetModulationAndSensitivity(&(state)->phase, modulation, sensitivity)

cc_s16f FM_Operator_Process(const FM_Operator *fm_operator, cc_u8f amplitude_modulation, cc_u8f amplitude_modulation_shift, cc_s16f phase_modulation);

#endif /* FM_OPERATOR_H */
