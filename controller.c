#include "controller.h"

#include <string.h>

static void Controller_DoMicroseconds(Controller* const controller, const cc_u16f microseconds)
{
	if (controller->countdown >= microseconds)
	{
		controller->countdown -= microseconds;
	}
	else
	{
		controller->countdown = 0;
		controller->strobes = 0;
	}
}

static cc_bool Controller_GetButtonBit(const Controller_Callback callback, const void *user_data, const Controller_Button button)
{
	return !callback((void*)user_data, button);
}

void Controller_Initialise(Controller* const controller)
{
	memset(controller, 0, sizeof(*controller));
}

cc_u8f Controller_Read(Controller* const controller, const cc_u16f microseconds, const Controller_Callback callback, const void *user_data)
{
	Controller_DoMicroseconds(controller, microseconds);

	if (controller->th_bit)
	{
		switch (controller->strobes)
		{
			default:
				return (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_C) << 5)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_B) << 4)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_RIGHT) << 3)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_LEFT) << 2)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_DOWN) << 1)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_UP) << 0);

			case 3:
				return (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_C) << 5)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_B) << 4)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_MODE) << 3)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_X) << 2)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_Y) << 1)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_Z) << 0);
		}
	}
	else
	{
		switch (controller->strobes)
		{
			default:
				return (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_START) << 5)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_A) << 4)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_DOWN) << 1)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_UP) << 0);

			case 2:
				return (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_START) << 5)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_A) << 4);

			case 3:
				return (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_START) << 5)
				     | (Controller_GetButtonBit(callback, user_data, CONTROLLER_BUTTON_A) << 4)
				     | 0xF;
		}
	}
}

void Controller_Write(Controller* const controller, const cc_u8f value, const cc_u16f microseconds)
{
	const cc_bool new_th_bit = (value & 0x40) != 0;

	Controller_DoMicroseconds(controller, microseconds);

	/* TODO: The 1us latch time! Doesn't Decap Attack rely on that? */
	if (new_th_bit && !controller->th_bit)
	{
		controller->strobes = (controller->strobes + 1) % 4;
		controller->countdown = 1500; /* 1.5ms */
	}

	controller->th_bit = new_th_bit;
}
