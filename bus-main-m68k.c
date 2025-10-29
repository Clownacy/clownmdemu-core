#include "bus-main-m68k.h"

#include <assert.h>

#include "bus-sub-m68k.h"
#include "bus-z80.h"
#include "io-port.h"
#include "log.h"

/* The Z80 can trigger 68k bus errors by using the 68k address space window, so print its program counter here too. */
#define LOG_MAIN_CPU_BUS_ERROR_MESSAGE_PREFIX "[M68K PC: 0x%06" CC_PRIXLEAST32 ", Z80 PC: 0x%04" CC_PRIXLEAST16 "] "
#define LOG_MAIN_CPU_BUS_ERROR_ARGUMENTS clownmdemu->state->m68k.state.program_counter, clownmdemu->state->z80.state.program_counter
#define LOG_MAIN_CPU_BUS_ERROR_0(MESSAGE)                   LogMessage(LOG_MAIN_CPU_BUS_ERROR_MESSAGE_PREFIX MESSAGE, LOG_MAIN_CPU_BUS_ERROR_ARGUMENTS);
#define LOG_MAIN_CPU_BUS_ERROR_1(MESSAGE, ARG1)             LogMessage(LOG_MAIN_CPU_BUS_ERROR_MESSAGE_PREFIX MESSAGE, LOG_MAIN_CPU_BUS_ERROR_ARGUMENTS, ARG1);
#define LOG_MAIN_CPU_BUS_ERROR_2(MESSAGE, ARG1, ARG2)       LogMessage(LOG_MAIN_CPU_BUS_ERROR_MESSAGE_PREFIX MESSAGE, LOG_MAIN_CPU_BUS_ERROR_ARGUMENTS, ARG1, ARG2);
#define LOG_MAIN_CPU_BUS_ERROR_3(MESSAGE, ARG1, ARG2, ARG3) LogMessage(LOG_MAIN_CPU_BUS_ERROR_MESSAGE_PREFIX MESSAGE, LOG_MAIN_CPU_BUS_ERROR_ARGUMENTS, ARG1, ARG2, ARG3);

/* https://github.com/Clownacy/clownmdemu-mcd-boot */
static const cc_u16l megacd_boot_rom[] = {
#include "mega-cd-boot-rom.c"
};

static cc_u16f GetHCounterValue(const ClownMDEmu* const clownmdemu, const CycleMegaDrive target_cycle)
{
	/* TODO: V30 and PAL and H32. */

	/* TODO: This entire thing is a disgusting hack. */
	/* Once the VDP emulator becames slot-based, this junk should be erased. */
	const cc_u16f cycles_per_scanline = GetMegaDriveCyclesPerFrame(clownmdemu).cycle / GetTelevisionVerticalResolution(clownmdemu);

	/* Sourced from https://gendev.spritesmind.net/forum/viewtopic.php?t=3058. */
	const cc_u16f maximum_value = 0x100 - 0x30;

	return (target_cycle.cycle % cycles_per_scanline) * maximum_value / cycles_per_scanline;
}

static cc_bool GetHBlankBit(const ClownMDEmu* const clownmdemu, const CycleMegaDrive target_cycle)
{
	/* TODO: V30 and PAL and H32. */
	/* Sourced from https://plutiedev.com/mirror/kabuto-hardware-notes. */
	return GetHCounterValue(clownmdemu, target_cycle) > 0xB2;
}

static cc_u16f VDPReadCallback(void* const user_data, const cc_u32f address, const cc_u32f target_cycle)
{
	return M68kReadCallbackWithCycleWithDMA(user_data, address / 2, cc_true, cc_true, MakeCycleMegaDrive(target_cycle), cc_true);
}

static void VDPKDebugCallback(void* const user_data, const char* const string)
{
	(void)user_data;

	LogMessage("KDEBUG: %s", string);
}

void SyncM68k(const ClownMDEmu* const clownmdemu, CPUCallbackUserData* const other_state, const CycleMegaDrive target_cycle)
{
	const cc_u32f current_cycle = other_state->sync.m68k.current_cycle;

	if (target_cycle.cycle > current_cycle)
	{
		const cc_u32f m68k_cycles_to_do = (target_cycle.cycle - current_cycle) / CLOWNMDEMU_M68K_CLOCK_DIVIDER;

		Clown68000_ReadWriteCallbacks m68k_read_write_callbacks;

		m68k_read_write_callbacks.read_callback = M68kReadCallback;
		m68k_read_write_callbacks.write_callback = M68kWriteCallback;
		m68k_read_write_callbacks.user_data = other_state;

		other_state->sync.m68k.base_cycle = current_cycle;
		other_state->sync.m68k.current_cycle = current_cycle + m68k_cycles_to_do * CLOWNMDEMU_M68K_CLOCK_DIVIDER;

		Clown68000_DoCycles(clownmdemu->m68k, &m68k_read_write_callbacks, m68k_cycles_to_do);
	}
}

static cc_u32f GetBankedCartridgeAddress(const ClownMDEmu* const clownmdemu, const cc_u32f address)
{
	const cc_u32f masked_address = address & 0x3FFFFF;
	const cc_u32f bank_size = 512 * 1024; /* 512KiB */
	const cc_u32f bank_index = masked_address / bank_size;
	const cc_u32f bank_offset = masked_address % bank_size;
	return clownmdemu->state->cartridge_bankswitch[bank_index] * bank_size + bank_offset;
}

static cc_bool FrontendControllerCallback(void* const user_data, const Controller_Button button)
{
	ClownMDEmu_Button frontend_button;

	const IOPortToController_Parameters* const parameters = (const IOPortToController_Parameters*)user_data;
	const ClownMDEmu_Callbacks* const frontend_callbacks = parameters->frontend_callbacks;

	switch (button)
	{
		case CONTROLLER_BUTTON_UP:
			frontend_button = CLOWNMDEMU_BUTTON_UP;
			break;

		case CONTROLLER_BUTTON_DOWN:
			frontend_button = CLOWNMDEMU_BUTTON_DOWN;
			break;

		case CONTROLLER_BUTTON_LEFT:
			frontend_button = CLOWNMDEMU_BUTTON_LEFT;
			break;

		case CONTROLLER_BUTTON_RIGHT:
			frontend_button = CLOWNMDEMU_BUTTON_RIGHT;
			break;

		case CONTROLLER_BUTTON_A:
			frontend_button = CLOWNMDEMU_BUTTON_A;
			break;

		case CONTROLLER_BUTTON_B:
			frontend_button = CLOWNMDEMU_BUTTON_B;
			break;

		case CONTROLLER_BUTTON_C:
			frontend_button = CLOWNMDEMU_BUTTON_C;
			break;

		case CONTROLLER_BUTTON_X:
			frontend_button = CLOWNMDEMU_BUTTON_X;
			break;

		case CONTROLLER_BUTTON_Y:
			frontend_button = CLOWNMDEMU_BUTTON_Y;
			break;

		case CONTROLLER_BUTTON_Z:
			frontend_button = CLOWNMDEMU_BUTTON_Z;
			break;

		case CONTROLLER_BUTTON_START:
			frontend_button = CLOWNMDEMU_BUTTON_START;
			break;

		case CONTROLLER_BUTTON_MODE:
			frontend_button = CLOWNMDEMU_BUTTON_MODE;
			break;

		default:
			assert(cc_false);
			return cc_false;
	}

	return frontend_callbacks->input_requested((void*)frontend_callbacks->user_data, parameters->joypad_index, frontend_button);
}

static cc_u8f IOPortToController_ReadCallback(void* const user_data, const cc_u16f cycles)
{
	const IOPortToController_Parameters *parameters = (const IOPortToController_Parameters*)user_data;

	return Controller_Read(parameters->controller, cycles, FrontendControllerCallback, parameters);
}

static void IOPortToController_WriteCallback(void* const user_data, const cc_u8f value, const cc_u16f cycles)
{
	const IOPortToController_Parameters *parameters = (const IOPortToController_Parameters*)user_data;

	Controller_Write(parameters->controller, value, cycles);
}

cc_u16f M68kReadCallbackWithCycleWithDMA(const void* const user_data, const cc_u32f address_word, const cc_bool do_high_byte, const cc_bool do_low_byte, const CycleMegaDrive target_cycle, const cc_bool is_vdp_dma)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	const ClownMDEmu_Callbacks* const frontend_callbacks = clownmdemu->callbacks;
	const cc_u32f address = address_word * 2;

	cc_u16f value = 0;

	switch (address / 0x200000)
	{
		case 0x000000 / 0x200000:
		case 0x200000 / 0x200000:
		case 0x400000 / 0x200000:
		case 0x600000 / 0x200000:
			/* Cartridge, Mega CD. */
			if (((address & 0x400000) == 0) != clownmdemu->state->mega_cd.boot_from_cd)
			{
				if ((address & 0x200000) != 0 && clownmdemu->state->external_ram.mapped_in)
				{
					/* External RAM */
					const cc_u32f index = address & 0x1FFFFF;

					if (index >= clownmdemu->state->external_ram.size)
					{
						/* TODO: According to Genesis Plus GX, SRAM is actually mirrored past its end. */
						value = 0xFFFF;
						LOG_MAIN_CPU_BUS_ERROR_2("Attempted to read past the end of external RAM (0x%" CC_PRIXFAST32 " when the external RAM ends at 0x%" CC_PRIXLEAST32 ")", index, clownmdemu->state->external_ram.size);
					}
					else
					{
						value |= clownmdemu->state->external_ram.buffer[index + 0] << 8;
						value |= clownmdemu->state->external_ram.buffer[index + 1] << 0;
					}
				}
				else
				{
					/* Cartridge */
					const cc_u32f cartridge_address = GetBankedCartridgeAddress(clownmdemu, address);
					const cc_u32f cartridge_address_word = cartridge_address / 2;

					if (cartridge_address_word >= clownmdemu->cartridge_buffer_length)
					{
						LOG_MAIN_CPU_BUS_ERROR_1("MAIN-CPU attempted to read from beyond the end of the cartridge (offset 0x%" CC_PRIXFAST32 ")", cartridge_address);
					}
					else
					{
						value = clownmdemu->cartridge_buffer[cartridge_address_word];
					}
				}
			}
			else
			{
				if ((address & 0x200000) != 0)
				{
					/* WORD-RAM */
					if (clownmdemu->state->mega_cd.word_ram.in_1m_mode)
					{
						if ((address & 0x20000) != 0)
						{
							/* TODO */
							LOG_MAIN_CPU_BUS_ERROR_0("MAIN-CPU attempted to read from that weird half of 1M WORD-RAM");
						}
						else
						{
							value = clownmdemu->state->mega_cd.word_ram.buffer[(address_word & 0xFFFF) * 2 + clownmdemu->state->mega_cd.word_ram.ret];
						}
					}
					else
					{
						if (!clownmdemu->state->mega_cd.word_ram.ret)
						{
							LOG_MAIN_CPU_BUS_ERROR_0("MAIN-CPU attempted to read from WORD-RAM while SUB-CPU has it");
						}
						else
						{
							value = clownmdemu->state->mega_cd.word_ram.buffer[address_word & 0x1FFFF];
						}
					}

					if (is_vdp_dma)
					{
						/* Delay WORD-RAM DMA transfers. This is a real bug on the Mega CD that games have to work around. */
						/* This can easily be seen in Sonic CD's FMVs. */
						const cc_u16f delayed_value = value;

						value = clownmdemu->state->mega_cd.delayed_dma_word;
						clownmdemu->state->mega_cd.delayed_dma_word = delayed_value;
					}
				}
				else if ((address & 0x20000) == 0)
				{
					/* Mega CD BIOS */
					if ((address & 0x1FFFF) == 0x72)
					{
						/* The Mega CD has this strange hack in its bug logic, which allows
						   the H-Int interrupt address to be overridden with a register. */
						value = clownmdemu->state->mega_cd.hblank_address;
					}
					else
					{
						value = megacd_boot_rom[address_word & 0xFFFF];
					}
				}
				else
				{
					/* PRG-RAM */
					if (!clownmdemu->state->mega_cd.m68k.bus_requested)
					{
						LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from PRG-RAM while SUB-CPU has it");
					}
					else
					{
						value = clownmdemu->state->mega_cd.prg_ram.buffer[0x10000 * clownmdemu->state->mega_cd.prg_ram.bank + (address_word & 0xFFFF)];
					}
				}
			}

			break;

		case 0x800000 / 0x200000:
			/* 32X? */
			LOG_MAIN_CPU_BUS_ERROR_1("Attempted to read invalid 68k address 0x%" CC_PRIXFAST32, address);
			break;

		case 0xA00000 / 0x200000:
			/* IO region. */
			if (address >= 0xA00000 && address <= 0xA0FFFF)
			{
				/* Z80 RAM and YM2612 */
				if (!clownmdemu->state->z80.bus_requested)
				{
					LOG_MAIN_CPU_BUS_ERROR_0("68k attempted to read Z80 memory/YM2612 ports without Z80 bus");
				}
				else if (clownmdemu->state->z80.reset_held)
				{
					/* TODO: Does this actually bother real hardware? */
					/* TODO: According to Devon, yes it does. */
					LOG_MAIN_CPU_BUS_ERROR_0("68k attempted to read Z80 memory/YM2612 ports while Z80 reset request was active");
				}
				else
				{
					/* This is unnecessary, as the Z80 bus will have to have been requested, causing a sync. */
					/*SyncZ80(clownmdemu, callback_user_data, target_cycle);*/

					if (do_high_byte && do_low_byte)
						LOG_MAIN_CPU_BUS_ERROR_0("68k attempted to perform word-sized read of Z80 memory/YM2612 ports; the read word will only contain the first byte repeated");

					value = Z80ReadCallbackWithCycle(user_data, (address + (do_high_byte ? 0 : 1)) & 0x7FFF, target_cycle);
					value = value << 8 | value;

					/* TODO: This should delay the 68k by a cycle. */
					/* https://gendev.spritesmind.net/forum/viewtopic.php?p=29929&sid=7c86823ea17db0dca9238bb3fe32c93f#p29929 */
				}
			}
			else if (address >= 0xA10000 && address <= 0xA1001F)
			{
				/* I/O AREA */
				/* TODO: The missing ports. */
				/* TODO: Detect when this is accessed without obtaining the Z80 bus and log a warning. */
				/* TODO: According to 'gen-hw.txt', these can be accessed by their high bytes too. */
				switch (address)
				{
					case 0xA10000:
						if (do_low_byte)
							value |= ((clownmdemu->configuration->general.region == CLOWNMDEMU_REGION_OVERSEAS) << 7) | ((clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL) << 6) | (0 << 5);	/* Bit 5 clear = Mega CD attached */

						break;

					case 0xA10002:
					case 0xA10004:
					case 0xA10006:
						/* TODO: 'genhw.txt' mentions that even addresses should have valid data too? */
						if (do_low_byte)
						{
							IOPortToController_Parameters parameters;

							const cc_u16f joypad_index = (address - 0xA10002) / 2;
							const IOPort_ReadCallback read_callback = joypad_index < 2 ? IOPortToController_ReadCallback : NULL;

							parameters.controller = &clownmdemu->state->controllers[joypad_index];
							parameters.frontend_callbacks = frontend_callbacks;
							parameters.joypad_index = joypad_index;

							value = IOPort_ReadData(&clownmdemu->state->io_ports[joypad_index], SyncCommon(&callback_user_data->sync.io_ports[joypad_index], target_cycle.cycle, CLOWNMDEMU_MASTER_CLOCK_NTSC / 1000000), read_callback, &parameters);
						}

						break;

					case 0xA10008:
					case 0xA1000A:
					case 0xA1000C:
						if (do_low_byte)
						{
							const cc_u16f joypad_index = (address - 0xA10008) / 2;

							value = IOPort_ReadControl(&clownmdemu->state->io_ports[joypad_index]);
						}

						break;
				}
			}
			else if (address == 0xA11000)
			{
				/* MEMORY MODE */
				/* TODO */
				/* https://gendev.spritesmind.net/forum/viewtopic.php?p=28843&sid=65d8f210be331ff257a43b4e3dddb7c3#p28843 */
				/* According to this, this flag is only functional on earlier models, and effectively halves the 68k's speed when running from cartridge. */
			}
			else if (address == 0xA11100)
			{
				/* Z80 BUSREQ */
				/* On real hardware, bus requests do not complete if a reset is being held. */
				/* http://gendev.spritesmind.net/forum/viewtopic.php?f=2&t=2195 */
				const cc_bool z80_bus_obtained = clownmdemu->state->z80.bus_requested && !clownmdemu->state->z80.reset_held;

				if (clownmdemu->state->z80.reset_held)
					LOG_MAIN_CPU_BUS_ERROR_0("Z80 bus request will never end as long as the reset is asserted");

				/* TODO: According to Charles MacDonald's gen-hw.txt, the upper byte is actually the upper byte
					of the next instruction and the lower byte is just 0 (and the flag bit, of course). */
				value = 0xFF ^ z80_bus_obtained;
				value = value << 8 | value;
			}
			else if (address == 0xA11200)
			{
				/* Z80 RESET */
				/* TODO: According to Charles MacDonald's gen-hw.txt, the upper byte is actually the upper byte
					of the next instruction and the lower byte is just 0 (and the flag bit, of course). */
				value = 0xFF ^ clownmdemu->state->z80.reset_held;
				value = value << 8 | value;
			}
			else if (address == 0xA12000)
			{
				/* RESET, HALT */
				value = ((cc_u16f)clownmdemu->state->mega_cd.irq.enabled[1] << 15) |
					((cc_u16f)clownmdemu->state->mega_cd.m68k.bus_requested << 1) |
					((cc_u16f)!clownmdemu->state->mega_cd.m68k.reset_held << 0);
			}
			else if (address == 0xA12002)
			{
				/* Memory mode / Write protect */
				value = ((cc_u16f)clownmdemu->state->mega_cd.prg_ram.write_protect << 8) | ((cc_u16f)clownmdemu->state->mega_cd.prg_ram.bank << 6) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.in_1m_mode << 2) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.dmna << 1) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.ret << 0);
			}
			else if (address == 0xA12004)
			{
				/* CDC mode */
				value = CDC_Mode(&clownmdemu->state->mega_cd.cdc, cc_false);
			}
			else if (address == 0xA12006)
			{
				/* H-INT vector */
				value = clownmdemu->state->mega_cd.hblank_address;
			}
			else if (address == 0xA12008)
			{
				/* CDC host data */
				value = CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_false);
			}
			else if (address == 0xA1200C)
			{
				/* Stop watch */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from stop watch register");
			}
			else if (address == 0xA1200E)
			{
				/* Communication flag */
				SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
				value = clownmdemu->state->mega_cd.communication.flag;
			}
			else if (address >= 0xA12010 && address < 0xA12020)
			{
				/* Communication command */
				SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
				value = clownmdemu->state->mega_cd.communication.command[(address - 0xA12010) / 2];
			}
			else if (address >= 0xA12020 && address < 0xA12030)
			{
				/* Communication status */
				SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
				value = clownmdemu->state->mega_cd.communication.status[(address - 0xA12020) / 2];
			}
			else if (address == 0xA12030)
			{
				/* Timer W/INT3 */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from Timer W/INT3 register");
			}
			else if (address == 0xA12032)
			{
				/* Interrupt mask control */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from interrupt mask control register");
			}
			else if (address == 0xA130F0)
			{
				/* External RAM control */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from external RAM control register");
			}
			else if (address >= 0xA130F2 && address <= 0xA13100)
			{
				/* Cartridge bankswitching */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from cartridge bankswitch register");
			}
			else
			{
				LOG_MAIN_CPU_BUS_ERROR_1("Attempted to read invalid 68k address 0x%" CC_PRIXFAST32, address);
			}

			break;

		case 0xC00000 / 0x200000:
			/* VDP. */
			/* TODO: According to Charles MacDonald's gen-hw.txt, the VDP stuff is mirrored in the following pattern:
			MSB                       LSB
			110n n000 nnnn nnnn 000m mmmm

			'1' - This bit must be 1.
			'0' - This bit must be 0.
			'n' - This bit can have any value.
			'm' - VDP addresses (00-1Fh) */
			switch (address_word - 0xC00000 / 2)
			{
				case 0 / 2:
				case 2 / 2:
					/* VDP data port */
					/* TODO: Reading from the data port causes real Mega Drives to crash (if the VDP isn't in read mode). */
					value = VDP_ReadData(&clownmdemu->vdp);
					break;

				case 4 / 2:
				case 6 / 2:
					/* VDP control port */
					value = VDP_ReadControl(&clownmdemu->vdp);

					/* Temporary stupid hack: shove the PAL bit in here. */
					/* TODO: This should be moved to the VDP core once it becomes sensitive to PAL mode differences. */
					value |= (clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL);

					/* Temporary stupid hack: approximate the H-blank bit timing. */
					/* TODO: This should be moved to the VDP core once it becomes slot-based. */
					value |= GetHBlankBit(clownmdemu, target_cycle) << 2;

					break;

				case 8 / 2:
				{
					/* H/V COUNTER */
					/* TODO: The V counter emulation is incredibly inaccurate: the timing is likely wrong, and it should be incremented while in the blanking areas too. */
					/* TODO: Apparently in interlace mode 1, the lowest bit of the V-counter is set to the hidden ninth bit. */
					const cc_u8f h_counter = GetHCounterValue(clownmdemu, target_cycle);
					const cc_u8f v_counter = clownmdemu->state->vdp.double_resolution_enabled
						? ((clownmdemu->state->current_scanline & 0x7F) << 1) | ((clownmdemu->state->current_scanline & 0x80) >> 7)
						: (clownmdemu->state->current_scanline & 0xFF);
					value = v_counter << 8 | h_counter;
					break;
				}

				case 0x10 / 2:
				case 0x12 / 2:
				case 0x14 / 2:
				case 0x16 / 2:
					/* PSG */
					/* TODO: What's supposed to happen here, if you read from the PSG? */
					/* TODO: It freezes the 68k, that's what:
						https://forums.sonicretro.org/index.php?posts/1066059/ */
					LOG_MAIN_CPU_BUS_ERROR_0("Attempted to read from PSG; this will freeze a real Mega Drive");
					break;

				default:
					LOG_MAIN_CPU_BUS_ERROR_1("Attempted to read invalid 68k address 0x%" CC_PRIXFAST32, address);
					break;
			}

			break;

		case 0xE00000 / 0x200000:
			/* WORK-RAM. */
			value = clownmdemu->state->m68k.ram[address_word % CC_COUNT_OF(clownmdemu->state->m68k.ram)];
			break;
	}

	return value;
}

cc_u16f M68kReadCallbackWithCycle(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte, const CycleMegaDrive target_cycle)
{
	return M68kReadCallbackWithCycleWithDMA(user_data, address, do_high_byte, do_low_byte, target_cycle, cc_false);
}

cc_u16f M68kReadCallback(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u32f current_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	return M68kReadCallbackWithCycleWithDMA(user_data, address, do_high_byte, do_low_byte, MakeCycleMegaDrive(callback_user_data->sync.m68k.base_cycle + current_cycle * CLOWNMDEMU_M68K_CLOCK_DIVIDER), cc_false);
}

void M68kWriteCallbackWithCycle(const void* const user_data, const cc_u32f address_word, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u16f value, const CycleMegaDrive target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	const ClownMDEmu_Callbacks* const frontend_callbacks = clownmdemu->callbacks;
	const cc_u32f address = address_word * 2;

	const cc_u16f high_byte = (value >> 8) & 0xFF;
	const cc_u16f low_byte = (value >> 0) & 0xFF;

	cc_u16f mask = 0;

	if (do_high_byte)
		mask |= 0xFF00;
	if (do_low_byte)
		mask |= 0x00FF;

	switch (address / 0x200000)
	{
		case 0x000000 / 0x200000:
		case 0x200000 / 0x200000:
		case 0x400000 / 0x200000:
		case 0x600000 / 0x200000:
			/* Cartridge, Mega CD. */
			if (((address & 0x400000) == 0) != clownmdemu->state->mega_cd.boot_from_cd)
			{
				if ((address & 0x200000) != 0 && clownmdemu->state->external_ram.mapped_in)
				{
					/* External RAM */
					const cc_u32f index = address & 0x1FFFFF;

					if (index >= clownmdemu->state->external_ram.size)
					{
						/* TODO: According to Genesis Plus GX, SRAM is actually mirrored past its end. */
						LOG_MAIN_CPU_BUS_ERROR_2("Attempted to write past the end of external RAM (0x%" CC_PRIXFAST32 " when the external RAM ends at 0x%" CC_PRIXLEAST32 ")", index, clownmdemu->state->external_ram.size);
					}
					else
					{
						switch (clownmdemu->state->external_ram.data_size)
						{
							case 0:
							case 2:
								if (do_high_byte)
									clownmdemu->state->external_ram.buffer[index + 0] = high_byte;
								break;
						}

						switch (clownmdemu->state->external_ram.data_size)
						{
							case 0:
							case 3:
								if (do_low_byte)
									clownmdemu->state->external_ram.buffer[index + 1] = low_byte;
								break;
						}
					}
				}
				else
				{
					/* Cartridge */
					const cc_u32f cartridge_address = GetBankedCartridgeAddress(clownmdemu, address);

					/* TODO: This is temporary, just to catch possible bugs in the 68k emulator */
					LOG_MAIN_CPU_BUS_ERROR_1("Attempted to write to ROM address 0x%" CC_PRIXFAST32, cartridge_address);
				}
			}
			else
			{
				if ((address & 0x200000) != 0)
				{
					/* WORD-RAM */
					if (clownmdemu->state->mega_cd.word_ram.in_1m_mode)
					{
						if ((address & 0x20000) != 0)
						{
							/* TODO */
							LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to that weird half of 1M WORD-RAM");
						}
						else
						{
							clownmdemu->state->mega_cd.word_ram.buffer[(address_word & 0xFFFF) * 2 + clownmdemu->state->mega_cd.word_ram.ret] &= ~mask;
							clownmdemu->state->mega_cd.word_ram.buffer[(address_word & 0xFFFF) * 2 + clownmdemu->state->mega_cd.word_ram.ret] |= value & mask;
						}
					}
					else
					{
						if (!clownmdemu->state->mega_cd.word_ram.ret)
						{
							LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to WORD-RAM while SUB-CPU has it");
						}
						else
						{
							clownmdemu->state->mega_cd.word_ram.buffer[address_word & 0x1FFFF] &= ~mask;
							clownmdemu->state->mega_cd.word_ram.buffer[address_word & 0x1FFFF] |= value & mask;
						}
					}
				}
				else if ((address & 0x20000) == 0)
				{
					/* Mega CD BIOS */
					LOG_MAIN_CPU_BUS_ERROR_1("Attempted to write to BIOS (0x%" CC_PRIXFAST32 ")", address);
				}
				else
				{
					/* PRG-RAM */
					const cc_u32f prg_ram_index = 0x10000 * clownmdemu->state->mega_cd.prg_ram.bank + (address_word & 0xFFFF);

					if (!clownmdemu->state->mega_cd.m68k.bus_requested)
					{
						LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to PRG-RAM while SUB-CPU has it");
					}
					else if (prg_ram_index < (cc_u32f)clownmdemu->state->mega_cd.prg_ram.write_protect * 0x200)
					{
						LOG_MAIN_CPU_BUS_ERROR_1("Attempted to write to write-protected portion of PRG-RAM (0x%" CC_PRIXFAST32 ")", prg_ram_index);
					}
					else
					{
						clownmdemu->state->mega_cd.prg_ram.buffer[prg_ram_index] &= ~mask;
						clownmdemu->state->mega_cd.prg_ram.buffer[prg_ram_index] |= value & mask;
					}
				}
			}

			break;

		case 0x800000 / 0x200000:
			/* 32X? */
			LOG_MAIN_CPU_BUS_ERROR_1("Attempted to write invalid 68k address 0x%" CC_PRIXFAST32, address);
			break;

		case 0xA00000 / 0x200000:
			/* IO region. */
			if (address >= 0xA00000 && address <= 0xA0FFFF)
			{
				/* Z80 RAM and YM2612 */
				if (!clownmdemu->state->z80.bus_requested)
				{
					LOG_MAIN_CPU_BUS_ERROR_0("68k attempted to write Z80 memory/YM2612 ports without Z80 bus");
				}
				else if (clownmdemu->state->z80.reset_held)
				{
					/* TODO: Does this actually bother real hardware? */
					/* TODO: According to Devon, yes it does. */
					LOG_MAIN_CPU_BUS_ERROR_0("68k attempted to write Z80 memory/YM2612 ports while Z80 reset request was active");
				}
				else
				{
					/* This is unnecessary, as the Z80 bus will have to have been requested, causing a sync. */
					/*SyncZ80(clownmdemu, callback_user_data, target_cycle);*/

					if (do_high_byte && do_low_byte)
						LOG_MAIN_CPU_BUS_ERROR_0("68k attempted to perform word-sized write of Z80 memory/YM2612 ports; only the top byte will be written");

					if (do_high_byte)
						Z80WriteCallbackWithCycle(user_data, (address + 0) & 0x7FFF, high_byte, target_cycle);
					else /*if (do_low_byte)*/
						Z80WriteCallbackWithCycle(user_data, (address + 1) & 0x7FFF, low_byte, target_cycle);

					/* TODO: This should delay the 68k by a cycle. */
					/* https://gendev.spritesmind.net/forum/viewtopic.php?p=29929&sid=7c86823ea17db0dca9238bb3fe32c93f#p29929 */
				}
			}
			else if (address >= 0xA10000 && address <= 0xA1001F)
			{
				/* I/O AREA */
				/* TODO */
				switch (address)
				{
					case 0xA10002:
					case 0xA10004:
					case 0xA10006:
						if (do_low_byte)
						{
							IOPortToController_Parameters parameters;

							const cc_u16f joypad_index = (address - 0xA10002) / 2;
							const IOPort_WriteCallback write_callback = joypad_index < 2 ? IOPortToController_WriteCallback : NULL;

							parameters.controller = &clownmdemu->state->controllers[joypad_index];
							parameters.frontend_callbacks = frontend_callbacks;
							parameters.joypad_index = joypad_index;

							IOPort_WriteData(&clownmdemu->state->io_ports[joypad_index], low_byte, CLOWNMDEMU_MASTER_CLOCK_NTSC / 1000000, write_callback, &parameters);
						}

						break;

					case 0xA10008:
					case 0xA1000A:
					case 0xA1000C:
						if (do_low_byte)
						{
							const cc_u16f joypad_index = (address - 0xA10008) / 2;

							IOPort_WriteControl(&clownmdemu->state->io_ports[joypad_index], low_byte);
						}

						break;
				}
			}
			else if (address == 0xA11000)
			{
				/* MEMORY MODE */
				/* TODO: Make setting this to DRAM mode make the cartridge writeable. */
			}
			else if (address == 0xA11100)
			{
				/* Z80 BUSREQ */
				if (do_high_byte)
				{
					const cc_bool bus_request = (high_byte & 1) != 0;

					if (clownmdemu->state->z80.bus_requested != bus_request)
						SyncZ80(clownmdemu, callback_user_data, target_cycle);

					clownmdemu->state->z80.bus_requested = bus_request;
				}
			}
			else if (address == 0xA11200)
			{
				/* Z80 RESET */
				if (do_high_byte)
				{
					const cc_bool new_reset_held = (high_byte & 1) == 0;

					if (clownmdemu->state->z80.reset_held && !new_reset_held)
					{
						SyncZ80(clownmdemu, callback_user_data, target_cycle);
						Z80_Reset(clownmdemu->z80);
						FM_State_Initialise(&clownmdemu->state->fm);
					}

					clownmdemu->state->z80.reset_held = new_reset_held;
				}
			}
			else if (address == 0xA12000)
			{
				/* RESET, HALT */
				Clown68000_ReadWriteCallbacks m68k_read_write_callbacks;

				const cc_bool interrupt = (high_byte & (1 << 0)) != 0;
				const cc_bool bus_request = (low_byte & (1 << 1)) != 0;
				const cc_bool reset = (low_byte & (1 << 0)) == 0;

				m68k_read_write_callbacks.read_callback = MCDM68kReadCallback;
				m68k_read_write_callbacks.write_callback = MCDM68kWriteCallback;
				m68k_read_write_callbacks.user_data = callback_user_data;

				if (clownmdemu->state->mega_cd.m68k.bus_requested != bus_request)
					SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));

				if (clownmdemu->state->mega_cd.m68k.reset_held && !reset)
				{
					SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
					Clown68000_Reset(clownmdemu->mcd_m68k, &m68k_read_write_callbacks);
				}

				if (interrupt && clownmdemu->state->mega_cd.irq.enabled[1])
				{
					SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
					Clown68000_Interrupt(clownmdemu->mcd_m68k, 2);
				}

				clownmdemu->state->mega_cd.m68k.bus_requested = bus_request;
				clownmdemu->state->mega_cd.m68k.reset_held = reset;
			}
			else if (address == 0xA12002)
			{
				/* Memory mode / Write protect */
				/* TODO: Exact behaviour of DMNA and RET when toggling between 1M and 2M modes. */
				/* https://gendev.spritesmind.net/forum/viewtopic.php?p=15269#p15269 */
				if (do_high_byte)
					clownmdemu->state->mega_cd.prg_ram.write_protect = high_byte;

				if (do_low_byte)
				{
					const cc_bool dmna = (low_byte & (1 << 1)) != 0;

					/* Contrary to the official documentation, the DMNA bit needs to be set to 0 to request a 1M bank swap. */
					/* https://gendev.spritesmind.net/forum/viewtopic.php?p=16388#p16388 */
					if (dmna != clownmdemu->state->mega_cd.word_ram.in_1m_mode)
					{
						SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));

						clownmdemu->state->mega_cd.word_ram.dmna = cc_true;

						if (!clownmdemu->state->mega_cd.word_ram.in_1m_mode)
							clownmdemu->state->mega_cd.word_ram.ret = cc_false;
					}

					clownmdemu->state->mega_cd.prg_ram.bank = (low_byte >> 6) & 3;
				}
			}
			else if (address == 0xA12004)
			{
				/* CDC mode */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to CDC mode register");
			}
			else if (address == 0xA12006)
			{
				/* H-INT vector */
				clownmdemu->state->mega_cd.hblank_address &= ~mask;
				clownmdemu->state->mega_cd.hblank_address |= value & mask;
			}
			else if (address == 0xA12008)
			{
				/* CDC host data */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to CDC host data register");
			}
			else if (address == 0xA1200C)
			{
				/* Stop watch */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to stop watch register");
			}
			else if (address == 0xA1200E)
			{
				/* Communication flag */
				if (do_high_byte)
				{
					SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
					clownmdemu->state->mega_cd.communication.flag = (clownmdemu->state->mega_cd.communication.flag & 0x00FF) | (value & 0xFF00);
				}

				if (do_low_byte)
					LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to SUB-CPU's communication flag");
			}
			else if (address >= 0xA12010 && address < 0xA12020)
			{
				/* Communication command */
				SyncMCDM68k(clownmdemu, callback_user_data, CycleMegaDriveToMegaCD(clownmdemu, target_cycle));
				clownmdemu->state->mega_cd.communication.command[(address - 0xA12010) / 2] &= ~mask;
				clownmdemu->state->mega_cd.communication.command[(address - 0xA12010) / 2] |= value & mask;
			}
			else if (address >= 0xA12020 && address < 0xA12030)
			{
				/* Communication status */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to SUB-CPU's communication status");
			}
			else if (address == 0xA12030)
			{
				/* Timer W/INT3 */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to Timer W/INT3 register");
			}
			else if (address == 0xA12032)
			{
				/* Interrupt mask control */
				LOG_MAIN_CPU_BUS_ERROR_0("Attempted to write to interrupt mask control register");
			}
			else if (address == 0xA130F0)
			{
				/* External RAM control */
				/* TODO: Apparently this is actually two bit-packed flags! */
				/* https://forums.sonicretro.org/index.php?posts/622087/ */
				/* https://web.archive.org/web/20130731104452/http://emudocs.org/Genesis/ssf2.txt */
				/* TODO: Actually, the second bit only exists on devcarts? */
				/* https://forums.sonicretro.org/index.php?posts/1096788/ */
				if (do_low_byte && clownmdemu->state->external_ram.size != 0)
					clownmdemu->state->external_ram.mapped_in = low_byte != 0;
			}
			else if (address >= 0xA130F2 && address <= 0xA13100)
			{
				/* Cartridge bankswitching */
				if (do_low_byte)
					clownmdemu->state->cartridge_bankswitch[(address - 0xA130F0) / 2] = low_byte; /* We deliberately make index 0 inaccessible, as bank 0 is always set to 0 on real hardware. */
			}
			else
			{
				LOG_MAIN_CPU_BUS_ERROR_1("Attempted to write invalid 68k address 0x%" CC_PRIXFAST32, address);
			}

			break;

		case 0xC00000 / 0x200000:
			/* VDP. */
			switch (address_word - 0xC00000 / 2)
			{
				case 0 / 2:
				case 2 / 2:
					/* VDP data port */
					VDP_WriteData(&clownmdemu->vdp, value, frontend_callbacks->colour_updated, frontend_callbacks->user_data);
					break;

				case 4 / 2:
				case 6 / 2:
					/* VDP control port */
					VDP_WriteControl(&clownmdemu->vdp, value, frontend_callbacks->colour_updated, frontend_callbacks->user_data, VDPReadCallback, callback_user_data, VDPKDebugCallback, NULL, target_cycle.cycle);

					/* TODO: This should be done more faithfully once the CPU interpreters are bus-event-oriented. */
					RaiseHorizontalInterruptIfNeeded(clownmdemu);
					RaiseVerticalInterruptIfNeeded(clownmdemu);
					break;

				case 8 / 2:
					/* H/V COUNTER */
					/* TODO */
					break;

				case 0x10 / 2:
				case 0x12 / 2:
				case 0x14 / 2:
				case 0x16 / 2:
					/* PSG */
					if (do_low_byte)
					{
						SyncZ80(clownmdemu, callback_user_data, target_cycle);
						SyncPSG(callback_user_data, target_cycle);

						/* Alter the PSG's state */
						PSG_DoCommand(&clownmdemu->psg, low_byte);
					}
					break;

				case 0x18 / 2:
					VDP_WriteDebugControl(&clownmdemu->vdp, value);
					break;

				case 0x1C / 2:
					VDP_WriteDebugData(&clownmdemu->vdp, value);
					break;

				default:
					LOG_MAIN_CPU_BUS_ERROR_1("Attempted to write invalid 68k address 0x%" CC_PRIXFAST32, address);
					break;
			}

			break;

		case 0xE00000 / 0x200000:
			/* WORK-RAM. */
			clownmdemu->state->m68k.ram[address_word % CC_COUNT_OF(clownmdemu->state->m68k.ram)] &= ~mask;
			clownmdemu->state->m68k.ram[address_word % CC_COUNT_OF(clownmdemu->state->m68k.ram)] |= value & mask;
			break;
	}
}

void M68kWriteCallback(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u32f current_cycle, const cc_u16f value)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	M68kWriteCallbackWithCycle(user_data, address, do_high_byte, do_low_byte, value, MakeCycleMegaDrive(callback_user_data->sync.m68k.base_cycle + current_cycle * CLOWNMDEMU_M68K_CLOCK_DIVIDER));
}
