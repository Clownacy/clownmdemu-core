#ifndef FM_PHASE_H
#define FM_PHASE_H

#include "clowncommon.h"

typedef struct FM_Phase_State
{
	cc_u32f position;
	cc_u32f step;

	cc_u16f f_number_and_block;
	cc_u16f key_code;
	cc_u16f detune;
	cc_u16f multiplier;
} FM_Phase_State;

void FM_Phase_State_Initialise(FM_Phase_State *phase);

cc_u16f FM_Phase_GetKeyCode(const FM_Phase_State *phase);

void FM_Phase_SetFrequency(FM_Phase_State *phase, cc_u16f f_number_and_block);
void FM_Phase_SetDetuneAndMultiplier(FM_Phase_State *phase, cc_u16f detune, cc_u16f multiplier);

void FM_Phase_Reset(FM_Phase_State *phase);
cc_u32f FM_Phase_Increment(FM_Phase_State *phase);

#endif /* FM_PHASE_H */
