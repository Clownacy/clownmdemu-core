#include "controller-manager.h"

#include <assert.h>

void ControllerManager_Initialise(ControllerManager* const manager)
{
	unsigned int i;

	ControllerMultitapEA_Initialise(&manager->state.ea_multitap);

	for (i = 0; i < CC_COUNT_OF(manager->state.sega_multitaps); ++i)
		ControllerMultitapSega_Initialise(&manager->state.sega_multitaps[i]);
}

cc_u8f ControllerManager_Read(ControllerManager* const manager, const cc_u8f port_index, const cc_u16f microseconds, const Controller_Callback callback, const void* const user_data)
{
	assert(port_index < 2);

	switch (manager->configuration.protocol)
	{
		case CONTROLLER_MANAGER_PROTOCOL_STANDARD:
			return ControllerMultitapEA_ReadController(&manager->state.ea_multitap, port_index, microseconds, callback, user_data);

		case CONTROLLER_MANAGER_PROTOCOL_EA_4_WAY_PLAY:
			return ControllerMultitapEA_ReadPort(&manager->state.ea_multitap, port_index, microseconds, callback, user_data);

		case CONTROLLER_MANAGER_PROTOCOL_SEGA_TAP:
			return ControllerMultitapSega_Read(&manager->state.sega_multitaps[port_index], callback, user_data);
	}

	/* Just a placeholder fall-back value. */
	return 0xFF;
}

void ControllerManager_Write(ControllerManager* const manager, const cc_u8f port_index, const cc_u16f microseconds, const cc_u8f value)
{
	assert(port_index < 2);

	switch (manager->configuration.protocol)
	{
		case CONTROLLER_MANAGER_PROTOCOL_STANDARD:
			ControllerMultitapEA_WriteController(&manager->state.ea_multitap, port_index, microseconds, value);
			break;

		case CONTROLLER_MANAGER_PROTOCOL_EA_4_WAY_PLAY:
			ControllerMultitapEA_WritePort(&manager->state.ea_multitap, port_index, microseconds, value);
			break;

		case CONTROLLER_MANAGER_PROTOCOL_SEGA_TAP:
			ControllerMultitapSega_Write(&manager->state.sega_multitaps[port_index], value);
			break;
	}
}
