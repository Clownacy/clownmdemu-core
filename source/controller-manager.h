#ifndef CONTROLLER_MANAGER_H
#define CONTROLLER_MANAGER_H

#include "controller-multitap-ea.h"
#include "controller-multitap-sega.h"

typedef enum ControllerManager_Protocol
{
	CONTROLLER_MANAGER_PROTOCOL_STANDARD,
	CONTROLLER_MANAGER_PROTOCOL_SEGA_TAP,
	CONTROLLER_MANAGER_PROTOCOL_EA_4_WAY_PLAY
} ControllerManager_Protocol;

typedef struct ControllerManager_Configuration
{
	ControllerManager_Protocol protocol;
} ControllerManager_Configuration;

typedef struct ControllerManager_State
{
	ControllerMultitapEA ea_multitap;
	ControllerMultitapSega sega_multitaps[2];
} ControllerManager_State;

typedef struct ControllerManager
{
	ControllerManager_Configuration configuration;
	ControllerManager_State state;
} ControllerManager;

void ControllerManager_Initialise(ControllerManager *manager);
cc_u8f ControllerManager_Read(ControllerManager *manager, cc_u8f port_index, cc_u16f microseconds, Controller_Callback callback, const void *user_data);
void ControllerManager_Write(ControllerManager *manager, cc_u8f port_index, cc_u16f microseconds, cc_u8f value);

#endif /* CONTROLLER_MANAGER_H */
