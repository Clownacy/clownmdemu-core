#ifndef CONTROLLER_MULTITAP_H
#define CONTROLLER_MULTITAP_H

#include "clowncommon/clowncommon.h"

#include "controller.h"

typedef cc_bool (*ControllerMultitap_Callback)(void *user_data, cc_u8f controller_index, Controller_Button button);

typedef struct ControllerMultitap
{
	cc_bool th_bit, tl_bit;
	cc_u8l pulses;
} ControllerMultitap;

void ControllerMultitap_Initialise(ControllerMultitap *multitap);
cc_u8f ControllerMultitap_Read(ControllerMultitap *multitap, ControllerMultitap_Callback callback, const void *user_data);
void ControllerMultitap_Write(ControllerMultitap *multitap, cc_u8f value);

#endif /* CONTROLLER_MULTITAP_H */
