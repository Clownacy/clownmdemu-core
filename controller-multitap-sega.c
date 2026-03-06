#include "controller-multitap-sega.h"

static cc_bool ControllerMultitapSega_GetButtonBit(const Controller_Callback callback, const void* const user_data, const cc_u8f controller_index, const Controller_Button button)
{
	return !callback((void*)user_data, controller_index, button);
}

static cc_u8f ControllerMultitapSega_GetButtonNybble(const Controller_Callback callback, const void* const user_data, const cc_u8f controller_index, const Controller_Button* const buttons)
{
	cc_u8f i, value = 0;

	for (i = 0; i < 4; ++i)
	{
		value <<= 1;
		value |= ControllerMultitapSega_GetButtonBit(callback, user_data, controller_index, buttons[i]);
	}

	return value;
}

void ControllerMultitapSega_Initialise(ControllerMultitapSega* const multitap)
{
	multitap->th_bit = multitap->tl_bit = cc_false;
	multitap->pulses = 0;
}

static cc_u8f ControllerMultitapSega_GetNybble(ControllerMultitapSega* const multitap, const Controller_Callback callback, const void* const user_data)
{
	if (multitap->th_bit)
		return 3;

	switch (multitap->pulses)
	{
		case 0:
			return 0xF;

		case 1:
		case 2:
			/* Fixed identification value, apparently. */
			return 0;

		case 3:
		case 4:
		case 5:
		case 6:
			/* Input IDs (all 6-button Control Pads, for now). */
			return 1;
			
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
		{
			static const Controller_Button buttons[3][4] = {
				{CONTROLLER_BUTTON_RIGHT, CONTROLLER_BUTTON_LEFT, CONTROLLER_BUTTON_DOWN, CONTROLLER_BUTTON_UP},
				{CONTROLLER_BUTTON_START, CONTROLLER_BUTTON_A,    CONTROLLER_BUTTON_C,    CONTROLLER_BUTTON_B },
				{CONTROLLER_BUTTON_MODE,  CONTROLLER_BUTTON_X,    CONTROLLER_BUTTON_Y,    CONTROLLER_BUTTON_Z },
			};
			
			const cc_u8f button_index     = (multitap->pulses - 7) % 3;
			const cc_u8f controller_index = (multitap->pulses - 7) / 3;

			return ControllerMultitapSega_GetButtonNybble(callback, user_data, controller_index, buttons[button_index]);
		}
	}

	/* Fallback value. */
	return 0xF;
}

cc_u8f ControllerMultitapSega_Read(ControllerMultitapSega* const multitap, const Controller_Callback callback, const void* const user_data)
{
	return (multitap->tl_bit << 4) | ControllerMultitapSega_GetNybble(multitap, callback, user_data);
}

void ControllerMultitapSega_Write(ControllerMultitapSega* const multitap, const cc_u8f value)
{
	const cc_bool new_tl_bit = (value & (1 << 5)) != 0;
	const cc_bool new_th_bit = (value & (1 << 6)) != 0;

	if (multitap->tl_bit != new_tl_bit)
	{
		multitap->tl_bit = new_tl_bit;

		++multitap->pulses;
	}

	if (multitap->th_bit != new_th_bit)
	{
		multitap->th_bit = new_th_bit;

		multitap->pulses = 0;
	}
}
