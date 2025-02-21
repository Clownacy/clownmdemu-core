#include "bus-z80.h"

#include <assert.h>

#include "bus-main-m68k.h"
#include "log.h"

/* TODO: https://sonicresearch.org/community/index.php?threads/help-with-potentially-extra-ram-space-for-z80-sound-drivers.6763/#post-89797 */

static cc_u16f SyncZ80Callback(const ClownMDEmu* const clownmdemu, void* const user_data)
{
	return CLOWNMDEMU_Z80_CLOCK_DIVIDER * Z80_DoCycle(&clownmdemu->z80, (const Z80_ReadAndWriteCallbacks*)user_data);
}

void SyncZ80(const ClownMDEmu* const clownmdemu, CPUCallbackUserData* const other_state, const CycleMegaDrive target_cycle)
{
	const cc_bool z80_not_running = clownmdemu->state->z80.bus_requested || clownmdemu->state->z80.reset_held;

	Z80_ReadAndWriteCallbacks z80_read_write_callbacks;

	z80_read_write_callbacks.read = Z80ReadCallback;
	z80_read_write_callbacks.write = Z80WriteCallback;
	z80_read_write_callbacks.user_data = other_state;

	SyncCPUCommon(clownmdemu, &other_state->sync.z80, target_cycle.cycle, z80_not_running, SyncZ80Callback, &z80_read_write_callbacks);
}

cc_u16f Z80ReadCallbackWithCycle(const void* const user_data, const cc_u16f address, const CycleMegaDrive target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;

	/* I suppose, on real hardware, in an open-bus situation, this would actually
	   be a static variable that retains its value from the last call. */
	cc_u16f value;

	value = 0;

	if (address < 0x2000)
	{
		value = clownmdemu->state->z80.ram[address];
	}
	else if (address >= 0x4000 && address <= 0x4003)
	{
		/* YM2612 */
		/* TODO: Model 1 Mega Drives only do this for 0x4000. Accessing other ports will return the old status, and only for a short time. See Nuked OPN2 for more details. */
		value = SyncFM(callback_user_data, target_cycle);
	}
	else if (address == 0x6000 || address == 0x6001)
	{
		/* TODO: Does this do *anything*? */
	}
	else if (address == 0x7F11)
	{
		/* PSG */
		/* TODO */
	}
	else if (address >= 0x8000)
	{
		/* 68k ROM window (actually a window into the 68k's address space: you can access the PSG through it IIRC). */
		const cc_u32f m68k_address = ((cc_u32f)clownmdemu->state->z80.bank * 0x8000) + (address & 0x7FFE);

		SyncM68k(clownmdemu, callback_user_data, target_cycle);

		if ((address & 1) != 0)
			value = M68kReadCallbackWithCycle(user_data, m68k_address / 2, cc_false, cc_true, target_cycle);
		else
			value = M68kReadCallbackWithCycle(user_data, m68k_address / 2, cc_true, cc_false, target_cycle) >> 8;
	}
	else
	{
		LogMessage("Attempted to read invalid Z80 address 0x%" CC_PRIXFAST16 " at 0x%" CC_PRIXLEAST16, address, clownmdemu->state->z80.state.program_counter);
	}

	return value;
}

cc_u16f Z80ReadCallback(const void* const user_data, const cc_u16f address)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	return Z80ReadCallbackWithCycle(user_data, address, MakeCycleMegaDrive(callback_user_data->sync.z80.current_cycle));
}

void Z80WriteCallbackWithCycle(const void* const user_data, const cc_u16f address, const cc_u16f value, const CycleMegaDrive target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;

	if (address < 0x2000)
	{
		clownmdemu->state->z80.ram[address] = value;
	}
	else if (address >= 0x4000 && address <= 0x4003)
	{
		/* YM2612 */
		const cc_u16f port = (address & 2) != 0 ? 1 : 0;

		/* Update the FM up until this point in time. */
		SyncFM(callback_user_data, target_cycle);

		if ((address & 1) == 0)
			FM_DoAddress(&clownmdemu->fm, port, value);
		else
			FM_DoData(&clownmdemu->fm, value);
	}
	else if (address == 0x6000 || address == 0x6001)
	{
		clownmdemu->state->z80.bank >>= 1;
		clownmdemu->state->z80.bank |= (value & 1) != 0 ? 0x100 : 0;
	}
	else if (address == 0x7F11)
	{
		/* PSG (accessed through the 68k's bus). */
		SyncM68k(clownmdemu, callback_user_data, target_cycle);
		M68kWriteCallbackWithCycle(user_data, 0xC00010 / 2, cc_false, cc_true, value, target_cycle);
	}
	else if (address >= 0x8000)
	{
		/* TODO: Apparently Mamono Hunter Youko needs the Z80 to be able to write to 68k RAM in order to boot?
		   777 Casino also does weird stuff like this.
		   http://gendev.spritesmind.net/forum/viewtopic.php?f=24&t=347&start=30
		   http://gendev.spritesmind.net/forum/viewtopic.php?f=2&t=985 */

		/* 68k ROM window (actually a window into the 68k's address space: you can access the PSG through it IIRC). */
		/* TODO: Apparently the Z80 can access the IO ports and send a bus request to itself. */
		const cc_u32f m68k_address = ((cc_u32f)clownmdemu->state->z80.bank * 0x8000) + (address & 0x7FFE);

		SyncM68k(clownmdemu, callback_user_data, target_cycle);

		if ((address & 1) != 0)
			M68kWriteCallbackWithCycle(user_data, m68k_address / 2, cc_false, cc_true, value, target_cycle);
		else
			M68kWriteCallbackWithCycle(user_data, m68k_address / 2, cc_true, cc_false, value << 8, target_cycle);
	}
	else
	{
		LogMessage("Attempted to write invalid Z80 address 0x%" CC_PRIXFAST16 " at 0x%" CC_PRIXLEAST16, address, clownmdemu->state->z80.state.program_counter);
	}
}

void Z80WriteCallback(const void* const user_data, const cc_u16f address, const cc_u16f value)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	Z80WriteCallbackWithCycle(user_data, address, value, MakeCycleMegaDrive(callback_user_data->sync.z80.current_cycle));
}
