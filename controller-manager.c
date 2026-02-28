#include "controller-manager.h"

#include <assert.h>

void ControllerManager_Initialise(ControllerManager* const manager)
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(manager->controllers); ++i)
		Controller_Initialise(&manager->controllers[i]);
}

cc_u8f ControllerManager_Read(ControllerManager* const manager, const cc_u8f port_index, const cc_u16f microseconds, const Controller_Callback callback, const void *user_data)
{
	assert(port_index < CC_COUNT_OF(manager->controllers));
	return Controller_Read(&manager->controllers[port_index], microseconds, callback, user_data);
}

void ControllerManager_Write(ControllerManager* const manager, const cc_u8f port_index, const cc_u8f value, const cc_u16f microseconds)
{
	assert(port_index < CC_COUNT_OF(manager->controllers));
	Controller_Write(&manager->controllers[port_index], value, microseconds);
}
