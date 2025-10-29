#ifndef FM_PHASE_H
#define FM_PHASE_H

#include "clowncommon/clowncommon.h"

typedef struct FM_Phase
{
	cc_u32l position;
	cc_u32l step;

	cc_u16l f_number_and_block;
	cc_u16l key_code;
	cc_u16l detune;
	cc_u16l multiplier;
} FM_Phase;

void FM_Phase_Initialise(FM_Phase *phase);

#define FM_Phase_GetKeyCode(phase) ((phase)->key_code)

void FM_Phase_SetFrequency(FM_Phase *phase, cc_u8f modulation, cc_u8f sensitivity, cc_u16f f_number_and_block);
void FM_Phase_SetDetuneAndMultiplier(FM_Phase *phase, cc_u8f modulation, cc_u8f sensitivity, cc_u16f detune, cc_u16f multiplier);
void FM_Phase_SetModulationAndSensitivity(FM_Phase *phase, cc_u8f modulation, cc_u8f sensitivity);

#define FM_Phase_Reset(phase) ((phase)->position = 0)
#define FM_Phase_Increment(phase) ((phase)->position += (phase)->step)

#endif /* FM_PHASE_H */
