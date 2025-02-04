#ifndef FM_LFO_H
#define FM_LFO_H

#include "clowncommon/clowncommon.h"

typedef struct FM_LFO
{
	cc_u8l frequency;
	cc_u8l amplitude_modulation, phase_modulation;
	cc_u8l sub_counter, counter;
	cc_bool enabled;
} FM_LFO;

void FM_LFO_Initialise(FM_LFO *state);
void FM_LFO_Advance(FM_LFO *state);

#endif
