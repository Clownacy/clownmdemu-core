#ifndef CONTROLLER_MANAGER_H
#define CONTROLLER_MANAGER_H

#include "controller.h"

typedef struct ControllerManager
{
	Controller controllers[2];
} ControllerManager;

void ControllerManager_Initialise(ControllerManager *manager);
cc_u8f ControllerManager_Read(ControllerManager *manager, cc_u8f port_index, cc_u16f microseconds, Controller_Callback callback, const void *user_data);
void ControllerManager_Write(ControllerManager *manager, cc_u8f port_index, cc_u8f value, cc_u16f microseconds);

#endif /* CONTROLLER_MANAGER_H */
