#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "clowncommon/clowncommon.h"

typedef enum Controller_Button
{
	CONTROLLER_BUTTON_UP,
	CONTROLLER_BUTTON_DOWN,
	CONTROLLER_BUTTON_LEFT,
	CONTROLLER_BUTTON_RIGHT,
	CONTROLLER_BUTTON_A,
	CONTROLLER_BUTTON_B,
	CONTROLLER_BUTTON_C,
	CONTROLLER_BUTTON_X,
	CONTROLLER_BUTTON_Y,
	CONTROLLER_BUTTON_Z,
	CONTROLLER_BUTTON_START,
	CONTROLLER_BUTTON_MODE,
	CONTROLLER_BUTTON_TOTAL
} Controller_Button;

typedef cc_bool (*Controller_Callback)(void *user_data, Controller_Button button);

typedef struct Controller
{
	cc_u16l countdown;
	cc_u8l strobes;
	cc_bool th_bit;
} Controller;

void Controller_Initialise(Controller *controller);
cc_u8f Controller_Read(Controller *controller, Controller_Callback callback, const void *user_data);
void Controller_Write(Controller *controller, cc_u8f value);
void Controller_DoMicroseconds(Controller *controller, cc_u16f microseconds);

#endif /* CONTROLLER_H */
