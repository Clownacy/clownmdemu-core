#ifndef CONTROLLER_MULTITAP_SEGA_H
#define CONTROLLER_MULTITAP_SEGA_H

#include "../libraries/clowncommon/clowncommon.h"

#include "controller.h"

typedef struct ControllerMultitapSega
{
	cc_bool th_bit, tl_bit;
	cc_u8l pulses;
} ControllerMultitapSega;

void ControllerMultitapSega_Initialise(ControllerMultitapSega *multitap);
cc_u8f ControllerMultitapSega_Read(ControllerMultitapSega *multitap, Controller_Callback callback, const void *user_data);
void ControllerMultitapSega_Write(ControllerMultitapSega *multitap, cc_u8f value);

#endif /* CONTROLLER_MULTITAP_SEGA_H */
