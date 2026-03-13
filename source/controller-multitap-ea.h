#ifndef CONTROLLER_MULTITAP_EA_H
#define CONTROLLER_MULTITAP_EA_H

#include "../libraries/clowncommon/clowncommon.h"

#include "controller.h"

typedef struct ControllerMultitapEA
{
	Controller controllers[4];
	cc_u8l selected_controller;
} ControllerMultitapEA;

void ControllerMultitapEA_Initialise(ControllerMultitapEA *multitap);

cc_u8f ControllerMultitapEA_ReadPort(ControllerMultitapEA *multitap, cc_u8f port_index, cc_u16f microseconds, Controller_Callback callback, const void *user_data);
void ControllerMultitapEA_WritePort(ControllerMultitapEA *multitap, cc_u8f port_index, cc_u16f microseconds, cc_u8f value);

cc_u8f ControllerMultitapEA_ReadController(ControllerMultitapEA *multitap, cc_u8f controller_index, cc_u16f microseconds, Controller_Callback callback, const void *user_data);
void ControllerMultitapEA_WriteController(ControllerMultitapEA *multitap, cc_u8f controller_index, cc_u16f microseconds, cc_u8f value);

#endif /* CONTROLLER_MULTITAP_EA_H */
