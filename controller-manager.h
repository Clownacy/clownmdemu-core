#ifndef CONTROLLER_MANAGER_H
#define CONTROLLER_MANAGER_H

#include "controller.h"

typedef enum ControllerManager_Protocol
{
	CONTROLLER_MANAGER_PROTOCOL_STANDARD,
	CONTROLLER_MANAGER_PROTOCOL_EA_4_WAY_PLAY
} ControllerManager_Protocol;

typedef cc_bool (*ControllerManager_Callback)(void *user_data, cc_u8f controller_index, Controller_Button button);

typedef struct ControllerManager_Configuration
{
	ControllerManager_Protocol protocol;
} ControllerManager_Configuration;

typedef struct ControllerManager_State
{
	Controller controllers[4];
	struct
	{
		cc_u8l selected_controller;
	} ea_4_way_play;
} ControllerManager_State;

typedef struct ControllerManager
{
	ControllerManager_Configuration configuration;
	ControllerManager_State state;
} ControllerManager;

void ControllerManager_Initialise(ControllerManager *manager);
cc_u8f ControllerManager_Read(ControllerManager *manager, cc_u8f port_index, cc_u16f microseconds, ControllerManager_Callback callback, const void *user_data);
void ControllerManager_Write(ControllerManager *manager, cc_u8f port_index, cc_u8f value, cc_u16f microseconds);

#endif /* CONTROLLER_MANAGER_H */
