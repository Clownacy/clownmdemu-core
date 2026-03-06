#include "controller-multitap-ea.h"

#include <assert.h>

static cc_u8f ControllerMultitapEA_ControllerRead(ControllerMultitapEA* const multitap, const cc_u8f controller_index, const Controller_Callback callback, const void* const user_data)
{
	return Controller_Read(&multitap->controllers[controller_index], controller_index, callback, user_data);
}

static void ControllerMultitapEA_DoMicroseconds(ControllerMultitapEA* const multitap, const cc_u16f microseconds)
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(multitap->controllers); ++i)
		Controller_DoMicroseconds(&multitap->controllers[i], microseconds);
}

void ControllerMultitapEA_Initialise(ControllerMultitapEA* const multitap)
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(multitap->controllers); ++i)
		Controller_Initialise(&multitap->controllers[i]);

	multitap->selected_controller = 0;
}

cc_u8f ControllerMultitapEA_ReadPort(ControllerMultitapEA* const multitap, const cc_u8f port_index, const cc_u16f microseconds, const Controller_Callback callback, const void* const user_data)
{
	assert(port_index < 2);

	switch (port_index)
	{
		case 0:
			if (multitap->selected_controller > 3)
				return 0x7C; /* Identifier. */

			ControllerMultitapEA_DoMicroseconds(multitap, microseconds);
			return ControllerMultitapEA_ControllerRead(multitap, multitap->selected_controller, callback, user_data);

		case 1:
			/* No idea, mate. */
			/* TODO: This. */
			break;
	}

	/* Just a placeholder fall-back value. */
	return 0xFF;
}

void ControllerMultitapEA_WritePort(ControllerMultitapEA* const multitap, const cc_u8f port_index, const cc_u16f microseconds, const cc_u8f value)
{
	assert(port_index < 2);

	switch (port_index)
	{
		case 0:
			ControllerMultitapEA_DoMicroseconds(multitap, microseconds);
			Controller_Write(&multitap->controllers[multitap->selected_controller], value);
			break;

		case 1:
			multitap->selected_controller = (value >> 4) & 7;
			break;
	}
}

cc_u8f ControllerMultitapEA_ReadController(ControllerMultitapEA* const multitap, const cc_u8f controller_index, const cc_u16f microseconds, const Controller_Callback callback, const void* const user_data)
{
	assert(controller_index < CC_COUNT_OF(multitap->controllers));

	Controller_DoMicroseconds(&multitap->controllers[controller_index], microseconds);
	return ControllerMultitapEA_ControllerRead(multitap, controller_index, callback, user_data);
}

void ControllerMultitapEA_WriteController(ControllerMultitapEA* const multitap, const cc_u8f controller_index, const cc_u16f microseconds, const cc_u8f value)
{
	assert(controller_index < CC_COUNT_OF(multitap->controllers));

	Controller_DoMicroseconds(&multitap->controllers[controller_index], microseconds);
	Controller_Write(&multitap->controllers[controller_index], value);
}