#include "bus-z80.h"

#include <assert.h>

#include "bus-main-m68k.h"
#include "log.h"

/* TODO: https://sonicresearch.org/community/index.php?threads/help-with-potentially-extra-ram-space-for-z80-sound-drivers.6763/#post-89797 */

static cc_u16f SyncZ80Callback(ClownMDEmu* const clownmdemu, void* const user_data)
{
	return CLOWNMDEMU_Z80_CLOCK_DIVIDER * ClownZ80_DoInstruction(&clownmdemu->state.z80.state, (const ClownZ80_ReadAndWriteCallbacks*)user_data);
}

static void Z80LogCallback(void* const user_data, const char* const format, ...)
{
	va_list args;

	(void)user_data;

	va_start(args, format);
	LogMessageV(format, args);
	va_end(args);
}

void SyncZ80(ClownMDEmu* const clownmdemu, CPUCallbackUserData* const other_state, const CycleMegaDrive target_cycle)
{
	const cc_bool z80_not_running = clownmdemu->state.z80.bus_requested || clownmdemu->state.z80.reset_held;

	ClownZ80_ReadAndWriteCallbacks z80_read_write_callbacks;

	z80_read_write_callbacks.read = Z80ReadCallback;
	z80_read_write_callbacks.write = Z80WriteCallback;
	z80_read_write_callbacks.log = Z80LogCallback;
	z80_read_write_callbacks.user_data = other_state;

	SyncCPUCommon(clownmdemu, &other_state->sync.z80, target_cycle.cycle, z80_not_running, SyncZ80Callback, &z80_read_write_callbacks);
}

static cc_u16f M68kReadByte(const void* const user_data, const cc_u32f address, const CycleMegaDrive target_cycle)
{
	const cc_bool is_odd = (address & 1) != 0;

	return (M68kReadCallbackWithCycle(user_data, address / 2, !is_odd, is_odd, target_cycle) >> (is_odd ? 0 : 8)) & 0xFF;
}

cc_u16f Z80ReadCallbackWithCycle(const void* const user_data, const cc_u16f address, const CycleMegaDrive target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;

	/* I suppose, on real hardware, in an open-bus situation, this would actually
	   be a static variable that retains its value from the last call. */
	cc_u16f value;

	value = 0;

	/* Layout verified by'gen-hw.txt'. */
	switch (address / 0x2000)
	{
		case 0: /* 0x0000 */
		case 1: /* 0x2000 */
			value = clownmdemu->state.z80.ram[address % CC_COUNT_OF(clownmdemu->state.z80.ram)];
			break;

		case 2: /* 0x4000 */
			/* YM2612 */
			/* TODO: Model 1 Mega Drives only do this for 0x4000 (and not 0x4001, 0x4002, and 0x4003). Accessing other ports will return the old status, and only for a short time. See Nuked OPN2 for more details. */
			value = SyncFM(callback_user_data, target_cycle);
			break;

		case 3: /* 0x6000 */
			if (address < 0x7F00)
			{
				value = 0xFF;
			}
			else
			{
				/* VDP (accessed through the 68k's bus). */
				SyncM68k(clownmdemu, callback_user_data, target_cycle);
				value = M68kReadByte(user_data, 0xC00000 + (address & 0x1F), target_cycle);
			}
			break;

		case 4: /* 0x8000 */
		case 5: /* 0xA000 */
		case 6: /* 0xC000 */
		case 7: /* 0xE000 */
			/* 68k ROM window (actually a window into the 68k's address space: you can access the PSG through it IIRC). */
			SyncM68k(clownmdemu, callback_user_data, target_cycle);
			value = M68kReadByte(user_data, (cc_u32f)clownmdemu->state.z80.bank * 0x8000 | address % 0x8000, target_cycle);
			break;

		default:
			LogMessage("Attempted to read invalid Z80 address 0x%" CC_PRIXFAST16 " at 0x%" CC_PRIXLEAST16, address, clownmdemu->state.z80.state.program_counter);
			break;
	}

	return value;
}

cc_u16f Z80ReadCallback(void* const user_data, const cc_u16f address)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	return Z80ReadCallbackWithCycle(user_data, address, MakeCycleMegaDrive(callback_user_data->sync.z80.current_cycle));
}

static void M68kWriteByte(const void* const user_data, const cc_u32f address, const cc_u16f value, const CycleMegaDrive target_cycle)
{
	const cc_bool is_odd = (address & 1) != 0;

	M68kWriteCallbackWithCycle(user_data, address / 2, !is_odd, is_odd, value << (is_odd ? 0 : 8), target_cycle);
}

void Z80WriteCallbackWithCycle(const void* const user_data, const cc_u16f address, const cc_u16f value, const CycleMegaDrive target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;

	/* Layout verified by'gen-hw.txt'. */
	switch (address / 0x2000)
	{
		case 0: /* 0x0000 */
		case 1: /* 0x2000 */
			clownmdemu->state.z80.ram[address % CC_COUNT_OF(clownmdemu->state.z80.ram)] = value;
			break;

		case 2: /* 0x4000 */
			/* YM2612 */

			/* Update the FM up until this point in time. */
			SyncFM(callback_user_data, target_cycle);

			if ((address & 1) == 0)
				FM_DoAddress(&clownmdemu->state.fm, (address & 2) != 0 ? 1 : 0, value);
			else
				FM_DoData(&clownmdemu->state.fm, value);

			break;

		case 3: /* 0x6000 */
			if (address < 0x6100)
			{
				/* Z80 bank register. */
				clownmdemu->state.z80.bank >>= 1;
				clownmdemu->state.z80.bank |= (value & 1) != 0 ? 0x100 : 0;
			}
			else if (address < 0x7F00)
			{
				/* Ignored. */
			}
			else
			{
				/* VDP (accessed through the 68k's bus). */
				SyncM68k(clownmdemu, callback_user_data, target_cycle);
				M68kWriteByte(user_data, 0xC00000 + (address & 0x1F), value, target_cycle);
			}

			break;

		case 4: /* 0x8000 */
		case 5: /* 0xA000 */
		case 6: /* 0xC000 */
		case 7: /* 0xE000 */
			/* TODO: Apparently Mamono Hunter Youko needs the Z80 to be able to write to 68k RAM in order to boot?
				777 Casino also does weird stuff like this.
				http://gendev.spritesmind.net/forum/viewtopic.php?f=24&t=347&start=30
				http://gendev.spritesmind.net/forum/viewtopic.php?f=2&t=985 */

			/* 68k ROM window (actually a window into the 68k's address space: you can access the PSG through it IIRC). */
			/* TODO: Apparently the Z80 can access the IO ports and send a bus request to itself. */
			SyncM68k(clownmdemu, callback_user_data, target_cycle);
			M68kWriteByte(user_data,  (cc_u32f)clownmdemu->state.z80.bank * 0x8000 | address % 0x8000, value, target_cycle);
			break;

		default:
			LogMessage("Attempted to write invalid Z80 address 0x%" CC_PRIXFAST16 " at 0x%" CC_PRIXLEAST16, address, clownmdemu->state.z80.state.program_counter);
			break;
	}
}

void Z80WriteCallback(void* const user_data, const cc_u16f address, const cc_u16f value)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	Z80WriteCallbackWithCycle(user_data, address, value, MakeCycleMegaDrive(callback_user_data->sync.z80.current_cycle));
}
