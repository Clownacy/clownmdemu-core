/* TODO: */
/* https://bitbucket.org/eke/genesis-plus-gx/issues/29/mega-cd-support */
/* https://gendev.spritesmind.net/forum/viewtopic.php?t=3020 */

#include "bus-sub-m68k.h"

#include <assert.h>

#include "bus-main-m68k.h"
#include "log.h"

static cc_u16f MCDM68kReadWord(const void* const user_data, const cc_u32f address, const CycleMegaCD target_cycle)
{
	assert(address % 2 == 0);

	return MCDM68kReadCallbackWithCycle(user_data, (address & 0xFFFFFF) / 2, cc_true, cc_true, target_cycle);
}

static cc_u16f MCDM68kReadLongword(const void* const user_data, const cc_u32f address, const CycleMegaCD target_cycle)
{
	cc_u32f longword;
	longword = (cc_u32f)MCDM68kReadWord(user_data, address + 0, target_cycle) << 16;
	longword |= (cc_u32f)MCDM68kReadWord(user_data, address + 2, target_cycle) << 0;
	return longword;
}

static void MCDM68kWriteWord(const void* const user_data, const cc_u32f address, const cc_u16f value, const CycleMegaCD target_cycle)
{
	assert(address % 2 == 0);

	MCDM68kWriteCallbackWithCycle(user_data, (address & 0xFFFFFF) / 2, cc_true, cc_true, value, target_cycle);
}

static void ROMSEEK(const ClownMDEmu* const clownmdemu, const ClownMDEmu_Callbacks* const frontend_callbacks, const cc_u32f starting_sector, const cc_u32f total_sectors)
{
	CDC_Stop(&clownmdemu->state->mega_cd.cd.cdc);
	CDC_Seek(&clownmdemu->state->mega_cd.cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data, starting_sector, total_sectors);
	frontend_callbacks->cd_seeked((void*)frontend_callbacks->user_data, starting_sector);
}

static void CDCSTART(const ClownMDEmu* const clownmdemu, const ClownMDEmu_Callbacks* const frontend_callbacks)
{
	clownmdemu->state->mega_cd.cdda.playing = cc_false;
	CDC_Start(&clownmdemu->state->mega_cd.cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data);
}

/* TODO: Move this to its own file? */
static void MegaCDBIOSCall(const ClownMDEmu* const clownmdemu, const void* const user_data, const ClownMDEmu_Callbacks* const frontend_callbacks, const CycleMegaCD target_cycle)
{
	/* TODO: None of this shit is accurate at all. */
	/* TODO: Devon's notes on the CDC commands:
	   https://forums.sonicretro.org/index.php?posts/1052926/ */
	const cc_u16f command = clownmdemu->mcd_m68k->data_registers[0] & 0xFFFF;

	switch (command)
	{
		case 0x02:
			/* MSCSTOP */
			/* TODO: Make this actually stop and not just pause. */
			/* Fallthrough */
		case 0x03:
			/* MSCPAUSEON */
			clownmdemu->state->mega_cd.cdda.paused = cc_true;
			break;

		case 0x04:
			/* MSCPAUSEOFF */
			clownmdemu->state->mega_cd.cdda.paused = cc_false;
			break;

		case 0x11:
			/* MSCPLAY */
		case 0x12:
			/* MSCPLAY1 */
		case 0x13:
			/* MSCPLAYR */
		{
			const cc_u16f track_number = MCDM68kReadWord(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);

			clownmdemu->state->mega_cd.cdda.playing = cc_true;
			clownmdemu->state->mega_cd.cdda.paused = cc_false;

			frontend_callbacks->cd_track_seeked((void*)frontend_callbacks->user_data, track_number, command == 0x11 ? CLOWNMDEMU_CDDA_PLAY_ALL : command == 0x12 ? CLOWNMDEMU_CDDA_PLAY_ONCE : CLOWNMDEMU_CDDA_PLAY_REPEAT);
			break;
		}

		case 0x17:
		{
			/* ROMREAD */
			const cc_u32f starting_sector = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);

			ROMSEEK(clownmdemu, frontend_callbacks, starting_sector, 0);
			CDCSTART(clownmdemu, frontend_callbacks);
		}

		case 0x18:
		{
			/* ROMSEEK */
			const cc_u32f starting_sector = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);

			ROMSEEK(clownmdemu, frontend_callbacks, starting_sector, 0);
			break;
		}

		case 0x20:
		{
			/* ROMREADN */
			const cc_u32f starting_sector = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);
			const cc_u32f total_sectors = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 4, target_cycle);

			/* TODO: What does 0 total sectors do to a real BIOS? */
			ROMSEEK(clownmdemu, frontend_callbacks, starting_sector, total_sectors);
			CDCSTART(clownmdemu, frontend_callbacks);
			break;
		}

		case 0x21:
		{
			/* ROMREADE */
			const cc_u32f starting_sector = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);
			const cc_u32f last_sector = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 4, target_cycle);

			/* TODO: How does the official BIOS respond to a negative sector count? */
			ROMSEEK(clownmdemu, frontend_callbacks, starting_sector, last_sector < starting_sector ? 0 : last_sector - starting_sector);
			CDCSTART(clownmdemu, frontend_callbacks);
			break;
		}

		case 0x88:
			/* CDCSTART */
			CDCSTART(clownmdemu, frontend_callbacks);
			break;

		case 0x89:
			/* CDCSTOP */
			CDC_Stop(&clownmdemu->state->mega_cd.cd.cdc);
			break;

		case 0x8A:
			/* CDCSTAT */
			if (!CDC_Stat(&clownmdemu->state->mega_cd.cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data))
				clownmdemu->mcd_m68k->status_register |= 1; /* Set carry flag to signal that a sector is not ready. */
			else
				clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that there's a sector ready. */

			break;

		case 0x8B:
			/* CDCREAD */
			if (!CDC_Read(&clownmdemu->state->mega_cd.cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data, &clownmdemu->mcd_m68k->data_registers[0]))
			{
				/* Sonic Megamix 4.0b relies on this. */
				clownmdemu->mcd_m68k->status_register |= 1; /* Set carry flag to signal that a sector has not been prepared. */
			}
			else
			{
				clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that a sector has been prepared. */
			}

			break;

		case 0x8C:
			/* CDCTRN */
			if ((CDC_Mode(&clownmdemu->state->mega_cd.cd.cdc, cc_true) & 0x8000) != 0)
			{
				clownmdemu->mcd_m68k->status_register |= 1; /* Set carry flag to signal that there's not a sector ready. */
			}
			else
			{
				cc_u32f i;
				const cc_u32f sector_address = clownmdemu->mcd_m68k->address_registers[0];
				const cc_u32f header_address = clownmdemu->mcd_m68k->address_registers[1];

				MCDM68kWriteWord(user_data, header_address + 0, CDC_HostData(&clownmdemu->state->mega_cd.cd.cdc, cc_true), target_cycle);
				MCDM68kWriteWord(user_data, header_address + 2, CDC_HostData(&clownmdemu->state->mega_cd.cd.cdc, cc_true), target_cycle);

				for (i = 0; i < CDC_SECTOR_SIZE; i += 2)
					MCDM68kWriteWord(user_data, sector_address + i, CDC_HostData(&clownmdemu->state->mega_cd.cd.cdc, cc_true), target_cycle);

				clownmdemu->mcd_m68k->address_registers[0] = (clownmdemu->mcd_m68k->address_registers[0] + CDC_SECTOR_SIZE) & 0xFFFFFFFF;
				clownmdemu->mcd_m68k->address_registers[1] = (clownmdemu->mcd_m68k->address_registers[1] + 4) & 0xFFFFFFFF;
				clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that there's always a sector ready. */
			}

			break;

		case 0x8D:
			/* CDCACK */
			CDC_Ack(&clownmdemu->state->mega_cd.cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data);
			break;

		default:
			LogMessage("UNRECOGNISED BIOS CALL DETECTED (0x%02" CC_PRIXFAST16 ")", command);
			break;
	}
}

static cc_u16f SyncMCDM68kForRealCallback(const ClownMDEmu* const clownmdemu, void* const user_data)
{
	const Clown68000_ReadWriteCallbacks* const m68k_read_write_callbacks = (const Clown68000_ReadWriteCallbacks*)user_data;

	return CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER * Clown68000_DoCycle(clownmdemu->mcd_m68k, m68k_read_write_callbacks);
}

void SyncMCDM68kForReal(const ClownMDEmu* const clownmdemu, const Clown68000_ReadWriteCallbacks* const m68k_read_write_callbacks, const CycleMegaCD target_cycle)
{
	const cc_bool mcd_m68k_not_running = clownmdemu->state->mega_cd.m68k.bus_requested || clownmdemu->state->mega_cd.m68k.reset_held;

	CPUCallbackUserData* const other_state = (CPUCallbackUserData*)m68k_read_write_callbacks->user_data;

	SyncCPUCommon(clownmdemu, &other_state->sync.mcd_m68k, target_cycle.cycle, mcd_m68k_not_running, SyncMCDM68kForRealCallback, m68k_read_write_callbacks);
}

static cc_u16f SyncMCDM68kCallback(const ClownMDEmu* const clownmdemu, void* const user_data)
{
	const Clown68000_ReadWriteCallbacks* const m68k_read_write_callbacks = (const Clown68000_ReadWriteCallbacks*)user_data;
	CPUCallbackUserData* const other_state = (CPUCallbackUserData*)m68k_read_write_callbacks->user_data;
	CycleMegaCD current_cycle;

	/* Update the 68000 to this point in time. */
	current_cycle.cycle = other_state->sync.mcd_m68k_irq3.current_cycle;
	SyncMCDM68kForReal(clownmdemu, m68k_read_write_callbacks, current_cycle);

	/* Raise an interrupt. */
	if (clownmdemu->state->mega_cd.irq.enabled[2])
		Clown68000_Interrupt(clownmdemu->mcd_m68k, 3);

	return clownmdemu->state->mega_cd.irq.irq3_countdown_master;
}

void SyncMCDM68k(const ClownMDEmu* const clownmdemu, CPUCallbackUserData* const other_state, const CycleMegaCD target_cycle)
{
	Clown68000_ReadWriteCallbacks m68k_read_write_callbacks;

	m68k_read_write_callbacks.read_callback = MCDM68kReadCallback;
	m68k_read_write_callbacks.write_callback = MCDM68kWriteCallback;
	m68k_read_write_callbacks.user_data = other_state;

	/* In order to support the timer interrupt (IRQ3), we hijack this function to update an IRQ3 sync object instead. */
	/* This sync object will raise interrupts whilst also synchronising the 68000. */
	SyncCPUCommon(clownmdemu, &other_state->sync.mcd_m68k_irq3, target_cycle.cycle, cc_false, SyncMCDM68kCallback, &m68k_read_write_callbacks);

	/* Now that we're done with IRQ3, finish synchronising the 68000. */
	SyncMCDM68kForReal(clownmdemu, &m68k_read_write_callbacks, target_cycle);
}

cc_u16f MCDM68kReadCallbackWithCycle(const void* const user_data, const cc_u32f address_word, const cc_bool do_high_byte, const cc_bool do_low_byte, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	const ClownMDEmu_Callbacks* const frontend_callbacks = clownmdemu->callbacks;
	const cc_u32f address = address_word * 2;

	cc_u16f value = 0;

	(void)do_high_byte;
	(void)do_low_byte;

	if (/*address >= 0 &&*/ address < 0x80000)
	{
		/* PRG-RAM */
		if (address == 0x5F16 && clownmdemu->mcd_m68k->program_counter == 0x5F16)
		{
			/* BRAM call! */
			/* TODO: None of this shit is accurate at all. */
			const cc_u16f command = clownmdemu->mcd_m68k->data_registers[0] & 0xFFFF;

			switch (command)
			{
				case 0x00:
					/* BRMINIT */
					clownmdemu->mcd_m68k->status_register &= ~1; /* Formatted RAM is present. */
					/* Size of Backup RAM. */
					clownmdemu->mcd_m68k->data_registers[0] &= 0xFFFF0000;
					clownmdemu->mcd_m68k->data_registers[0] |= 0x100; /* Maximum officially-allowed size. */
					/* "Display strings". */
					/*clownmdemu->mcd_m68k->address_registers[1] = I have no idea; */
					break;

				case 0x01:
					/* BRMSTAT */
					clownmdemu->mcd_m68k->data_registers[0] &= 0xFFFF0000;
					clownmdemu->mcd_m68k->data_registers[1] &= 0xFFFF0000;
					break;

				case 0x02:
					/* BRMSERCH */
					clownmdemu->mcd_m68k->status_register |= 1; /* File not found */
					break;

				case 0x03:
					/* BRMREAD */
					clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					clownmdemu->mcd_m68k->data_registers[0] &= 0xFFFF0000;
					clownmdemu->mcd_m68k->data_registers[1] &= 0xFFFFFF00;
					break;

				case 0x04:
					/* BRMWRITE */
					clownmdemu->mcd_m68k->status_register |= 1; /* Error */
					break;

				case 0x05:
					/* BRMDEL */
					clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					break;

				case 0x06:
					/* BRMFORMAT */
					clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					break;

				case 0x07:
					/* BRMDIR */
					clownmdemu->mcd_m68k->status_register |= 1; /* Error */
					break;

				case 0x08:
					/* BRMVERIFY */
					clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					break;

				default:
					LogMessage("UNRECOGNISED BRAM CALL DETECTED (0x%02" CC_PRIXFAST16 ")", command);
					break;
			}

			value = 0x4E75; /* 'rts' instruction */
		}
		else if (address == 0x5F22 && clownmdemu->mcd_m68k->program_counter == 0x5F22)
		{
			/* BIOS call! */
			MegaCDBIOSCall(clownmdemu, user_data, frontend_callbacks, target_cycle);

			value = 0x4E75; /* 'rts' instruction */
		}
		else
		{
			value = clownmdemu->state->mega_cd.prg_ram.buffer[address_word];
		}
	}
	else if (address < 0xC0000)
	{
		/* WORD-RAM */
		if (clownmdemu->state->mega_cd.word_ram.in_1m_mode)
		{
			/* TODO. */
			LogMessage("SUB-CPU attempted to read from the weird half of 1M WORD-RAM at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else if (!clownmdemu->state->mega_cd.word_ram.dmna)
		{
			/* TODO: According to Page 24 of MEGA-CD HARDWARE MANUAL, this should cause the CPU to hang, just like the Z80 accessing the ROM during a DMA transfer. */
			LogMessage("SUB-CPU attempted to read from WORD-RAM while MAIN-CPU has it at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else
		{
			value = clownmdemu->state->mega_cd.word_ram.buffer[address_word & 0x1FFFF];
		}
	}
	else if (address < 0xE0000)
	{
		/* WORD-RAM */
		if (!clownmdemu->state->mega_cd.word_ram.in_1m_mode)
		{
			/* TODO. */
			LogMessage("SUB-CPU attempted to read from the 1M half of WORD-RAM in 2M mode at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else
		{
			value = clownmdemu->state->mega_cd.word_ram.buffer[(address_word & 0xFFFF) * 2 + !clownmdemu->state->mega_cd.word_ram.ret];
		}
	}
	else if (address >= 0xFF0000 && address < 0xFF8000)
	{
		if ((address & 0x2000) != 0)
		{
			/* PCM wave RAM */
			LogMessage("SUB-CPU attempted to read from PCM wave RAM at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
		}
		else
		{
			/* PCM register */
			SyncPCM(callback_user_data, target_cycle);
			value = (cc_u16f)PCM_ReadRegister(&clownmdemu->pcm, address_word & 0xFFF);
		}
	}
	else if (address == 0xFF8000)
	{
		/* Reset */
		/* TODO: Everything else here. */
		value = 1; /* Signal that the Mega CD is ready. */
	}
	else if (address == 0xFF8002)
	{
		/* Memory mode / Write protect */
		value = ((cc_u16f)clownmdemu->state->mega_cd.word_ram.in_1m_mode << 2) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.dmna << 1) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.ret << 0);
	}
	else if (address == 0xFF8004)
	{
		/* CDC mode / device destination */
		value = CDC_Mode(&clownmdemu->state->mega_cd.cd.cdc, cc_true);
	}
	else if (address == 0xFF8006)
	{
		/* H-INT vector */
		LogMessage("SUB-CPU attempted to read from H-INT vector register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF8008)
	{
		/* CDC host data */
		value = CDC_HostData(&clownmdemu->state->mega_cd.cd.cdc, cc_true);
	}
	else if (address == 0xFF800C)
	{
		/* Stop watch */
		LogMessage("SUB-CPU attempted to read from stop watch register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF800E)
	{
		/* Communication flag */
		SyncM68k(clownmdemu, callback_user_data, CycleMegaCDToMegaDrive(clownmdemu, target_cycle));
		value = clownmdemu->state->mega_cd.communication.flag;
	}
	else if (address >= 0xFF8010 && address < 0xFF8020)
	{
		/* Communication command */
		SyncM68k(clownmdemu, callback_user_data, CycleMegaCDToMegaDrive(clownmdemu, target_cycle));
		value = clownmdemu->state->mega_cd.communication.command[(address - 0xFF8010) / 2];
	}
	else if (address >= 0xFF8020 && address < 0xFF8030)
	{
		/* Communication status */
		SyncM68k(clownmdemu, callback_user_data, CycleMegaCDToMegaDrive(clownmdemu, target_cycle));
		value = clownmdemu->state->mega_cd.communication.status[(address - 0xFF8020) / 2];
	}
	else if (address == 0xFF8030)
	{
		/* Timer W/INT3 */
		LogMessage("SUB-CPU attempted to read from Timer W/INT3 register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF8032)
	{
		/* Interrupt mask control */
		cc_u8f i;

		value = 0;

		for (i = 0; i < CC_COUNT_OF(clownmdemu->state->mega_cd.irq.enabled); ++i)
			value |= (cc_u16f)clownmdemu->state->mega_cd.irq.enabled[i] << (1 + i);
	}
	else if (address == 0xFF8058)
	{
		/* Stamp data size */
		/* TODO */
		value = 0;
	}
	else if (address == 0xFF8064)
	{
		/* Image buffer vertical draw size */
		/* TODO */
		value = 0;
	}
	else if (address == 0xFF8066)
	{
		/* Trace vector base address */
		/* TODO */
	}
	else
	{
		LogMessage("Attempted to read invalid MCD 68k address 0x%" CC_PRIXFAST32 " at 0x%" CC_PRIXLEAST32, address, clownmdemu->mcd_m68k->program_counter);
	}

	return value;
}

cc_u16f MCDM68kReadCallback(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	return MCDM68kReadCallbackWithCycle(user_data, address, do_high_byte, do_low_byte, MakeCycleMegaCD(callback_user_data->sync.mcd_m68k.current_cycle));
}

void MCDM68kWriteCallbackWithCycle(const void* const user_data, const cc_u32f address_word, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u16f value, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	const cc_u32f address = address_word * 2;

	const cc_u16f high_byte = (value >> 8) & 0xFF;
	const cc_u16f low_byte = (value >> 0) & 0xFF;

	cc_u16f mask = 0;

	if (do_high_byte)
		mask |= 0xFF00;
	if (do_low_byte)
		mask |= 0x00FF;

	if (/*address >= 0 &&*/ address < 0x80000)
	{
		/* PRG-RAM */
		clownmdemu->state->mega_cd.prg_ram.buffer[address_word] &= ~mask;
		clownmdemu->state->mega_cd.prg_ram.buffer[address_word] |= value & mask;
	}
	else if (address < 0xC0000)
	{
		/* WORD-RAM */
		if (clownmdemu->state->mega_cd.word_ram.in_1m_mode)
		{
			/* TODO. */
			LogMessage("SUB-CPU attempted to write to the weird half of 1M WORD-RAM at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else if (!clownmdemu->state->mega_cd.word_ram.dmna)
		{
			LogMessage("SUB-CPU attempted to write to WORD-RAM while MAIN-CPU has it at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else
		{
			clownmdemu->state->mega_cd.word_ram.buffer[address_word & 0x1FFFF] &= ~mask;
			clownmdemu->state->mega_cd.word_ram.buffer[address_word & 0x1FFFF] |= value & mask;
		}
	}
	else if (address < 0xE0000)
	{
		/* WORD-RAM */
		if (!clownmdemu->state->mega_cd.word_ram.in_1m_mode)
		{
			/* TODO. */
			LogMessage("SUB-CPU attempted to write to the 1M half of WORD-RAM in 2M mode at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else
		{
			clownmdemu->state->mega_cd.word_ram.buffer[(address_word & 0xFFFF) * 2 + !clownmdemu->state->mega_cd.word_ram.ret] &= ~mask;
			clownmdemu->state->mega_cd.word_ram.buffer[(address_word & 0xFFFF) * 2 + !clownmdemu->state->mega_cd.word_ram.ret] |= value & mask;
		}
	}
	else if (address >= 0xFF0000 && address < 0xFF8000)
	{
		if (do_low_byte)
		{
			const cc_u16f masked_address = address_word & 0xFFF;

			SyncPCM(callback_user_data, target_cycle);

			if ((address & 0x2000) != 0)
			{
				/* PCM wave RAM */
				PCM_WriteWaveRAM(&clownmdemu->pcm, masked_address, low_byte);
			}
			else
			{
				/* PCM register */
				PCM_WriteRegister(&clownmdemu->pcm, masked_address, low_byte);
			}
		}
	}
	else if (address == 0xFF8002)
	{
		/* Memory mode / Write protect */
		if (do_low_byte)
		{
			const cc_bool ret = (value & (1 << 0)) != 0;

			SyncM68k(clownmdemu, callback_user_data, CycleMegaCDToMegaDrive(clownmdemu, target_cycle));

			clownmdemu->state->mega_cd.word_ram.in_1m_mode = (value & (1 << 2)) != 0;

			if (ret || clownmdemu->state->mega_cd.word_ram.in_1m_mode)
			{
				clownmdemu->state->mega_cd.word_ram.dmna = cc_false;
				clownmdemu->state->mega_cd.word_ram.ret = ret;
			}
		}
	}
	else if (address == 0xFF8004)
	{
		/* CDC mode / device destination */
		CDC_SetDeviceDestination(&clownmdemu->state->mega_cd.cd.cdc, high_byte & 7);
	}
	else if (address == 0xFF8006)
	{
		/* H-INT vector */
		LogMessage("SUB-CPU attempted to write to H-INT vector register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF8008)
	{
		/* CDC host data */
		LogMessage("SUB-CPU attempted to write to CDC host data register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF800C)
	{
		/* Stop watch */
		LogMessage("SUB-CPU attempted to write to stop watch register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF800E)
	{
		/* Communication flag */
		if (do_high_byte)
			LogMessage("SUB-CPU attempted to write to MAIN-CPU's communication flag at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);

		if (do_low_byte)
		{
			SyncM68k(clownmdemu, callback_user_data, CycleMegaCDToMegaDrive(clownmdemu, target_cycle));
			clownmdemu->state->mega_cd.communication.flag = (clownmdemu->state->mega_cd.communication.flag & 0xFF00) | (value & 0x00FF);
		}
	}
	else if (address >= 0xFF8010 && address < 0xFF8020)
	{
		/* Communication command */
		LogMessage("SUB-CPU attempted to write to MAIN-CPU's communication command at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address >= 0xFF8020 && address < 0xFF8030)
	{
		/* Communication status */
		SyncM68k(clownmdemu, callback_user_data, CycleMegaCDToMegaDrive(clownmdemu, target_cycle));
		clownmdemu->state->mega_cd.communication.status[(address - 0xFF8020) / 2] &= ~mask;
		clownmdemu->state->mega_cd.communication.status[(address - 0xFF8020) / 2] |= value & mask;
	}
	else if (address == 0xFF8030)
	{
		if (do_low_byte) /* TODO: Does setting just the upper byte cause this to be updated anyway? */
		{
			/* Timer W/INT3 */
			clownmdemu->state->mega_cd.irq.irq3_countdown_master = clownmdemu->state->mega_cd.irq.irq3_countdown = low_byte == 0 ? 0 : (low_byte + 1) * CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER * CLOWNMDEMU_PCM_SAMPLE_RATE_DIVIDER;
		}
	}
	else if (address == 0xFF8032)
	{
		/* Interrupt mask control */
		if (do_low_byte)
		{
			cc_u8f i;

			for (i = 0; i < CC_COUNT_OF(clownmdemu->state->mega_cd.irq.enabled); ++i)
				clownmdemu->state->mega_cd.irq.enabled[i] = (value & (1 << (1 + i))) != 0;

			if (!clownmdemu->state->mega_cd.irq.enabled[0])
				clownmdemu->state->mega_cd.irq.irq1_pending = cc_false;
		}
	}
	else if (address == 0xFF8058)
	{
		/* Stamp data size */
		/* TODO */
	}
	else if (address == 0xFF8064)
	{
		/* Image buffer vertical draw size */
		/* TODO */
	}
	else if (address == 0xFF8066)
	{
		/* Trace vector base address */
		/* TODO */
		if (clownmdemu->state->mega_cd.irq.enabled[0])
			clownmdemu->state->mega_cd.irq.irq1_pending = cc_true;
	}
	else
	{
		LogMessage("Attempted to write invalid MCD 68k address 0x%" CC_PRIXFAST32 " at 0x%" CC_PRIXLEAST32, address, clownmdemu->mcd_m68k->program_counter);
	}
}

void MCDM68kWriteCallback(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u16f value)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	MCDM68kWriteCallbackWithCycle(user_data, address, do_high_byte, do_low_byte, value, MakeCycleMegaCD(callback_user_data->sync.mcd_m68k.current_cycle));
}
