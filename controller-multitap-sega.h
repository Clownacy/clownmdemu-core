#ifndef CONTROLLER_MULTITAP_SEGA_H
#define CONTROLLER_MULTITAP_SEGA_H

#include "clowncommon/clowncommon.h"

#include "controller.h"

typedef cc_bool (*ControllerMultitapSega_Callback)(void *user_data, cc_u8f controller_index, Controller_Button button);

typedef struct ControllerMultitapSega
{
	cc_bool th_bit, tl_bit;
	cc_u8l pulses;
} ControllerMultitapSega;

void ControllerMultitapSega_Initialise(ControllerMultitapSega *multitap);
cc_u8f ControllerMultitapSega_Read(ControllerMultitapSega *multitap, ControllerMultitapSega_Callback callback, const void *user_data);
void ControllerMultitapSega_Write(ControllerMultitapSega *multitap, cc_u8f value);

#endif /* CONTROLLER_MULTITAP_SEGA_H */
