#include "controller-manager.h"

#include <assert.h>

typedef struct ControllerManager_ControllerUserData
{
	ControllerManager_Callback callback;
	const void *user_data;
	cc_u8f controller_index;
} ControllerManager_ControllerUserData;

static cc_bool ControllerManager_ControllerCallback(void* const user_data, const Controller_Button button)
{
	const ControllerManager_ControllerUserData* const controller_user_data = (const ControllerManager_ControllerUserData*)user_data;

	return controller_user_data->callback((void*)controller_user_data->user_data, controller_user_data->controller_index, button);
}

static cc_u8f ControllerManager_ControllerRead(ControllerManager* const manager, const cc_u8f controller_index, const ControllerManager_Callback callback, const void* const user_data)
{
	ControllerManager_ControllerUserData controller_user_data;
	controller_user_data.callback = callback;
	controller_user_data.user_data = user_data;
	controller_user_data.controller_index = controller_index;

	return Controller_Read(&manager->state.controllers[controller_index], ControllerManager_ControllerCallback, &controller_user_data);
}

static void ControllerManager_DoMicroseconds(ControllerManager* const manager, const cc_u16f microseconds)
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(manager->state.controllers); ++i)
		Controller_DoMicroseconds(&manager->state.controllers[i], microseconds);
}

void ControllerManager_Initialise(ControllerManager* const manager)
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(manager->state.controllers); ++i)
		Controller_Initialise(&manager->state.controllers[i]);

	manager->state.ea_4_way_play.selected_controller = 0;
}

cc_u8f ControllerManager_Read(ControllerManager* const manager, const cc_u8f port_index, const cc_u16f microseconds, const ControllerManager_Callback callback, const void* const user_data)
{
	assert(port_index < 2);

	switch (manager->configuration.protocol)
	{
		case CONTROLLER_MANAGER_PROTOCOL_STANDARD:
			Controller_DoMicroseconds(&manager->state.controllers[port_index], microseconds);
			return ControllerManager_ControllerRead(manager, port_index, callback, user_data);

		case CONTROLLER_MANAGER_PROTOCOL_EA_4_WAY_PLAY:
			switch (port_index)
			{
				case 0:
					if (manager->state.ea_4_way_play.selected_controller > 3)
						return 0x7C; /* Identifier. */

					ControllerManager_DoMicroseconds(manager, microseconds);
					return ControllerManager_ControllerRead(manager, manager->state.ea_4_way_play.selected_controller, callback, user_data);

				case 1:
					/* No idea, mate. */
					/* TODO: This. */
					break;
			}

			break;
	}

	/* Just a placeholder fall-back value. */
	return 0xFF;
}

void ControllerManager_Write(ControllerManager* const manager, const cc_u8f port_index, const cc_u8f value, const cc_u16f microseconds)
{
	assert(port_index < 2);

	switch (manager->configuration.protocol)
	{
		case CONTROLLER_MANAGER_PROTOCOL_STANDARD:
			Controller_DoMicroseconds(&manager->state.controllers[port_index], microseconds);
			Controller_Write(&manager->state.controllers[port_index], value);
			break;

		case CONTROLLER_MANAGER_PROTOCOL_EA_4_WAY_PLAY:
			switch (port_index)
			{
				case 0:
					ControllerManager_DoMicroseconds(manager, microseconds);
					Controller_Write(&manager->state.controllers[manager->state.ea_4_way_play.selected_controller], value);
					break;

				case 1:
					manager->state.ea_4_way_play.selected_controller = (value >> 4) & 7;
					break;
			}

			break;
	}
}
