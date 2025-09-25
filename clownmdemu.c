#include "clownmdemu.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "clowncommon/clowncommon.h"

#include "bus-main-m68k.h"
#include "bus-sub-m68k.h"
#include "bus-z80.h"
#include "clown68000/interpreter/clown68000.h"
#include "fm.h"
#include "log.h"
#include "low-pass-filter.h"
#include "psg.h"
#include "vdp.h"
#include "z80.h"

#define MAX_ROM_SIZE (1024 * 1024 * 4) /* 4MiB */

/* TODO: Merge this with the functions in 'cdc.c'. */
static cc_u32f ClownMDEmu_U16sToU32(const cc_u16l* const u16s)
{
	return (cc_u32f)u16s[0] << 16 | u16s[1];
}

static void CDSectorTo68kRAM(const ClownMDEmu_Callbacks* const callbacks, cc_u16l* const ram)
{
	callbacks->cd_sector_read((void*)callbacks->user_data, ram);
}

static void CDSectorsTo68kRAM(const ClownMDEmu_Callbacks* const callbacks, cc_u16l* const ram, const cc_u32f start, const cc_u32f length)
{
	cc_u32f i;

	callbacks->cd_seeked((void*)callbacks->user_data, start / CDC_SECTOR_SIZE);

	for (i = 0; i < CC_DIVIDE_CEILING(length, CDC_SECTOR_SIZE); ++i)
		CDSectorTo68kRAM(callbacks, &ram[i * CDC_SECTOR_SIZE / 2]);
}

void ClownMDEmu_Constant_Initialise(ClownMDEmu_Constant* const constant)
{
	Z80_Constant_Initialise(&constant->z80);
	VDP_Constant_Initialise(&constant->vdp);
	FM_Constant_Initialise(&constant->fm);
	PSG_Constant_Initialise(&constant->psg);
}

void ClownMDEmu_State_Initialise(ClownMDEmu_State* const state)
{
	cc_u16f i;

	/* M68K */
	/* A real console does not retain its RAM contents between games, as RAM
	   is cleared when the console is powered-off.
	   Failing to clear RAM causes issues with Sonic games and ROM-hacks,
	   which skip initialisation when a certain magic number is found in RAM. */
	memset(state->m68k.ram, 0, sizeof(state->m68k.ram));
	state->m68k.cycle_countdown = 1;
	state->m68k.h_int_pending = state->m68k.v_int_pending = cc_false;

	/* Z80 */
	Z80_State_Initialise(&state->z80.state);
	memset(state->z80.ram, 0, sizeof(state->z80.ram));
	state->z80.cycle_countdown = 1;
	state->z80.bank = 0;
	state->z80.bus_requested = cc_false; /* This should be false, according to Charles MacDonald's gen-hw.txt. */
	state->z80.reset_held = cc_true;

	VDP_State_Initialise(&state->vdp);
	FM_State_Initialise(&state->fm);
	PSG_State_Initialise(&state->psg);

	/* The standard Sega SDK bootcode uses this to detect soft-resets. */
	for (i = 0; i < CC_COUNT_OF(state->io_ports); ++i)
		IOPort_Initialise(&state->io_ports[i]);

	for (i = 0; i < CC_COUNT_OF(state->controllers); ++i)
		Controller_Initialise(&state->controllers[i]);

	state->external_ram.size = 0;
	state->external_ram.non_volatile = cc_false;
	state->external_ram.data_size = 0;
	state->external_ram.device_type = 0;
	state->external_ram.mapped_in = cc_false;

	for (i = 0; i < CC_COUNT_OF(state->cartridge_bankswitch); ++i)
		state->cartridge_bankswitch[i] = i;

	/* Mega CD */
	state->mega_cd.m68k.cycle_countdown = 1;
	state->mega_cd.m68k.bus_requested = cc_true;
	state->mega_cd.m68k.reset_held = cc_true;

	state->mega_cd.prg_ram.bank = 0;

	state->mega_cd.word_ram.in_1m_mode = cc_false;
	/* Page 24 of MEGA-CD HARDWARE MANUAL confirms this. */
	state->mega_cd.word_ram.dmna = cc_false;
	state->mega_cd.word_ram.ret = cc_true;

	state->mega_cd.communication.flag = 0;

	for (i = 0; i < CC_COUNT_OF(state->mega_cd.communication.command); ++i)
		state->mega_cd.communication.command[i] = 0;

	for (i = 0; i < CC_COUNT_OF(state->mega_cd.communication.status); ++i)
		state->mega_cd.communication.status[i] = 0;
	
	for (i = 0; i < CC_COUNT_OF(state->mega_cd.irq.enabled); ++i)
		state->mega_cd.irq.enabled[i] = cc_false;

	state->mega_cd.irq.irq1_pending = cc_false;
	state->mega_cd.irq.irq3_countdown_master = state->mega_cd.irq.irq3_countdown = 0;

	state->mega_cd.rotation.large_stamp_map = cc_false;
	state->mega_cd.rotation.large_stamp = cc_false;
	state->mega_cd.rotation.repeating_stamp_map = cc_false;
	state->mega_cd.rotation.stamp_map_address = 0;
	state->mega_cd.rotation.image_buffer_address = 0;
	state->mega_cd.rotation.image_buffer_width = 0;
	state->mega_cd.rotation.image_buffer_height = 0;
	state->mega_cd.rotation.image_buffer_height_in_tiles = 0;
	state->mega_cd.rotation.image_buffer_x_offset = 0;
	state->mega_cd.rotation.image_buffer_y_offset = 0;

	CDC_Initialise(&state->mega_cd.cdc);
	CDDA_Initialise(&state->mega_cd.cdda);
	PCM_State_Initialise(&state->mega_cd.pcm);

	state->mega_cd.boot_from_cd = cc_false;
	state->mega_cd.hblank_address = 0xFFFF;
	state->mega_cd.delayed_dma_word = 0;

	/* Low-pass filters. */
	LowPassFilter_FirstOrder_Initialise(state->low_pass_filters.fm, CC_COUNT_OF(state->low_pass_filters.fm));
	LowPassFilter_FirstOrder_Initialise(state->low_pass_filters.psg, CC_COUNT_OF(state->low_pass_filters.psg));
	LowPassFilter_SecondOrder_Initialise(state->low_pass_filters.pcm, CC_COUNT_OF(state->low_pass_filters.pcm));
}

void ClownMDEmu_Parameters_Initialise(ClownMDEmu* const clownmdemu, const ClownMDEmu_Configuration* const configuration, const ClownMDEmu_Constant* const constant, ClownMDEmu_State* const state, const ClownMDEmu_Callbacks* const callbacks)
{
	clownmdemu->configuration = configuration;
	clownmdemu->constant = constant;
	clownmdemu->state = state;
	clownmdemu->callbacks = callbacks;

	clownmdemu->cartridge_buffer = NULL;
	clownmdemu->cartridge_buffer_length = 0;

	clownmdemu->m68k = &state->m68k.state;

	clownmdemu->z80.constant = &constant->z80;
	clownmdemu->z80.state = &state->z80.state;

	clownmdemu->mcd_m68k = &state->mega_cd.m68k.state;

	clownmdemu->vdp.configuration = &configuration->vdp;
	clownmdemu->vdp.constant = &constant->vdp;
	clownmdemu->vdp.state = &state->vdp;

	FM_Parameters_Initialise(&clownmdemu->fm, &configuration->fm, &constant->fm, &state->fm);

	clownmdemu->psg.configuration = &configuration->psg;
	clownmdemu->psg.constant = &constant->psg;
	clownmdemu->psg.state = &state->psg;

	clownmdemu->pcm.configuration = &configuration->pcm;
	clownmdemu->pcm.state = &state->mega_cd.pcm;
}

/* Very useful H-Counter/V-Counter information:
   https://gendev.spritesmind.net/forum/viewtopic.php?t=3058
   https://gendev.spritesmind.net/forum/viewtopic.php?t=768 */

static cc_u16f CyclesUntilHorizontalSync(const ClownMDEmu* const clownmdemu)
{
	const cc_u16f h32_divider = 5;

	if (clownmdemu->state->vdp.h40_enabled)
	{
		const cc_u16f h40_divider = 4;
	
		const cc_u16f left_blanking   =   2 * h32_divider
		                              +  62 * h40_divider;
		const cc_u16f left_border     =  26 * h40_divider;
		const cc_u16f active_display  = 640 * h40_divider;
		const cc_u16f right_border    =  28 * h40_divider;
		const cc_u16f right_blanking  =  18 * h40_divider;
	/*	const cc_u16f horizontal_sync =  15 * h32_divider
		                              +   2 * h40_divider
		                              +  15 * h32_divider
		                              +   2 * h40_divider
		                              +  15 * h32_divider
		                              +   2 * h40_divider
		                              +  13 * h32_divider;*/

		return left_blanking + left_border + active_display + right_border + right_blanking;
	}
	else
	{	
		const cc_u16f left_blanking   =  48 * h32_divider;
		const cc_u16f left_border     =  26 * h32_divider;
		const cc_u16f active_display  = 512 * h32_divider;
		const cc_u16f right_border    =  28 * h32_divider;
		const cc_u16f right_blanking  =  18 * h32_divider;
	/*	const cc_u16f horizontal_sync =  52 * h32_divider;*/

		return left_blanking + left_border + active_display + right_border + right_blanking;
	}
}

void ClownMDEmu_Iterate(const ClownMDEmu* const clownmdemu)
{
	ClownMDEmu_State* const state = clownmdemu->state;

	const cc_u16f television_vertical_resolution = GetTelevisionVerticalResolution(clownmdemu);
	const cc_u16f console_vertical_resolution = VDP_GetScreenHeightInTilePairs(&state->vdp) * VDP_STANDARD_TILE_PAIR_HEIGHT;
	const CycleMegaDrive cycles_per_frame_mega_drive = GetMegaDriveCyclesPerFrame(clownmdemu);
	const cc_u16f cycles_per_scanline = cycles_per_frame_mega_drive.cycle / television_vertical_resolution;
	const cc_u16f cycles_until_horizontal_sync = CyclesUntilHorizontalSync(clownmdemu);
	const CycleMegaCD cycles_per_frame_mega_cd = MakeCycleMegaCD(clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_MCD_MASTER_CLOCK) : CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_MCD_MASTER_CLOCK));

	CPUCallbackUserData cpu_callback_user_data;
	cc_u8f h_int_counter;
	cc_u8f i;

	cpu_callback_user_data.clownmdemu = clownmdemu;
	cpu_callback_user_data.sync.m68k.current_cycle = 0;
	/* TODO: This is awful; stop doing this. */
	cpu_callback_user_data.sync.m68k.cycle_countdown = &state->m68k.cycle_countdown;
	cpu_callback_user_data.sync.z80.current_cycle = 0;
	cpu_callback_user_data.sync.z80.cycle_countdown = &state->z80.cycle_countdown;
	cpu_callback_user_data.sync.mcd_m68k.current_cycle = 0;
	cpu_callback_user_data.sync.mcd_m68k.cycle_countdown = &state->mega_cd.m68k.cycle_countdown;
	cpu_callback_user_data.sync.mcd_m68k_irq3.current_cycle = 0;
	cpu_callback_user_data.sync.mcd_m68k_irq3.cycle_countdown = &state->mega_cd.irq.irq3_countdown;
	cpu_callback_user_data.sync.fm.current_cycle = 0;
	cpu_callback_user_data.sync.psg.current_cycle = 0;
	cpu_callback_user_data.sync.pcm.current_cycle = 0;
	for (i = 0; i < CC_COUNT_OF(cpu_callback_user_data.sync.io_ports); ++i)
		cpu_callback_user_data.sync.io_ports[i].current_cycle = 0;

	/* Reload H-Int counter at the top of the screen, just like real hardware does */
	h_int_counter = state->vdp.h_int_interval;

	state->vdp.currently_in_vblank = cc_false;

	for (state->current_scanline = 0; state->current_scanline < television_vertical_resolution; ++state->current_scanline)
	{
		const cc_u16f scanline = state->current_scanline;
		const CycleMegaDrive current_cycle_minus_horizontal_sync = MakeCycleMegaDrive(cycles_per_scanline * scanline + cycles_until_horizontal_sync);
		const CycleMegaDrive current_cycle = MakeCycleMegaDrive(cycles_per_scanline * (1 + scanline));

		/* Sync the 68k, since it's the one thing that can influence the VDP */
		SyncM68k(clownmdemu, &cpu_callback_user_data, current_cycle_minus_horizontal_sync);

		if (scanline < console_vertical_resolution)
		{
			/* Fire a H-Int if we've reached the requested line */
			/* TODO: There is some strange behaviour surrounding how H-Int is asserted. */
			/* https://gendev.spritesmind.net/forum/viewtopic.php?t=183 */
			/* TODO: The interrupt should occur at the START of H-Blank, not the end. */
			/* Lemmings 2 appears to rely on this so that the V-counter is 1 less than it would otherwise be, or else the game will not boot. */
			/* http://gendev.spritesmind.net/forum/viewtopic.php?t=388&start=45 */
			/* TODO: Timing info here: */
			/* http://gendev.spritesmind.net/forum/viewtopic.php?p=8201#p8201 */
			/* http://gendev.spritesmind.net/forum/viewtopic.php?p=8443#p8443 */
			/* http://gendev.spritesmind.net/forum/viewtopic.php?t=3058 */
			/* http://gendev.spritesmind.net/forum/viewtopic.php?t=519 */
			if (h_int_counter-- == 0)
			{
				h_int_counter = state->vdp.h_int_interval;

				/* Do H-Int */
				state->m68k.h_int_pending = cc_true;
				RaiseHorizontalInterruptIfNeeded(clownmdemu);
			}
		}

		SyncM68k(clownmdemu, &cpu_callback_user_data, current_cycle);

		/* Only render scanlines and generate H-Ints for scanlines that the console outputs to */
		if (scanline < console_vertical_resolution)
		{
			if (state->vdp.double_resolution_enabled)
			{
				VDP_RenderScanline(&clownmdemu->vdp, scanline * 2 + 0, clownmdemu->callbacks->scanline_rendered, clownmdemu->callbacks->user_data);
				VDP_RenderScanline(&clownmdemu->vdp, scanline * 2 + 1, clownmdemu->callbacks->scanline_rendered, clownmdemu->callbacks->user_data);
			}
			else
			{
				VDP_RenderScanline(&clownmdemu->vdp, scanline, clownmdemu->callbacks->scanline_rendered, clownmdemu->callbacks->user_data);
			}
		}
		else if (scanline == console_vertical_resolution) /* Check if we have reached the end of the console-output scanlines */
		{
			/* Do V-Int */
			state->m68k.v_int_pending = cc_true;
			RaiseVerticalInterruptIfNeeded(clownmdemu);

			/* According to Charles MacDonald's gen-hw.txt, this occurs regardless of the 'v_int_enabled' setting. */
			SyncZ80(clownmdemu, &cpu_callback_user_data, current_cycle);
			Z80_Interrupt(&clownmdemu->z80, cc_true);

			/* Flag that we have entered the V-blank region */
			state->vdp.currently_in_vblank = cc_true;
		}
		else if (scanline == console_vertical_resolution + 1)
		{
			/* Assert the Z80 interrupt for a whole scanline. This has the side-effect of causing a second interrupt to occur if the handler exits quickly. */
			/* TODO: According to Vladikcomper, this interrupt should be asserted for roughly 171 Z80 cycles. */
			SyncZ80(clownmdemu, &cpu_callback_user_data, current_cycle);
			Z80_Interrupt(&clownmdemu->z80, cc_false);
		}
	}

	/* Update everything for the rest of the frame. */
	SyncM68k(clownmdemu, &cpu_callback_user_data, cycles_per_frame_mega_drive);
	SyncZ80(clownmdemu, &cpu_callback_user_data, cycles_per_frame_mega_drive);
	SyncMCDM68k(clownmdemu, &cpu_callback_user_data, cycles_per_frame_mega_cd);
	SyncFM(&cpu_callback_user_data, cycles_per_frame_mega_drive);
	SyncPSG(&cpu_callback_user_data, cycles_per_frame_mega_drive);
	SyncPCM(&cpu_callback_user_data, cycles_per_frame_mega_cd);
	SyncCDDA(&cpu_callback_user_data, clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(44100) : CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(44100));

	/* Fire IRQ1 if needed. */
	/* TODO: This is a hack. Look into when this interrupt should actually be done. */
	if (state->mega_cd.irq.irq1_pending)
	{
		state->mega_cd.irq.irq1_pending = cc_false;
		Clown68000_Interrupt(clownmdemu->mcd_m68k, 1);
	}

	/* TODO: This should be done 75 times a second (in sync with the CDD interrupt), not 60! */
	CDDA_UpdateFade(&state->mega_cd.cdda);
}

static cc_u16f ReadCartridgeWord(const ClownMDEmu* const clownmdemu, const cc_u32f address)
{
	const cc_u32f address_word = address / 2;

	if (address_word >= clownmdemu->cartridge_buffer_length)
		return 0;

	return clownmdemu->cartridge_buffer[address_word];
}

static cc_u32f ReadCartridgeLongWord(const ClownMDEmu* const clownmdemu, const cc_u32f address)
{
	cc_u32f longword;
	longword = ReadCartridgeWord(clownmdemu, address + 0) << 16;
	longword |= ReadCartridgeWord(clownmdemu, address + 2);
	return longword;
}

static cc_u32f NextPowerOfTwo(cc_u32f v)
{
	/* https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2 */
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

static void SetUpExternalRAM(const ClownMDEmu* const clownmdemu)
{
	ClownMDEmu_State* const state = clownmdemu->state;

	cc_u32f cartridge_base = 0;

	/* If external RAM metadata cannot be found in the ROM header, search for it in the locked-on cartridge instead. */
	/* This is needed for Sonic 3 & Knuckles to save data. */
	if (ReadCartridgeWord(clownmdemu, 0x1B0) != ((cc_u16f)'R' << 8 | (cc_u16f)'A' << 0))
		cartridge_base = ReadCartridgeLongWord(clownmdemu, 0x1D4) + 1;

	if ((cartridge_base & 1) == 0 && ReadCartridgeWord(clownmdemu, cartridge_base + 0x1B0) == ((cc_u16f)'R' << 8 | (cc_u16f)'A' << 0))
	{
		const cc_u16f metadata = ReadCartridgeWord(clownmdemu, cartridge_base + 0x1B2);
		const cc_u16f metadata_junk_bits = metadata & 0xA71F;
		const cc_u32f start = ReadCartridgeLongWord(clownmdemu, cartridge_base + 0x1B4);
		const cc_u32f end = ReadCartridgeLongWord(clownmdemu, cartridge_base + 0x1B8) + 1;
		const cc_u32f size = NextPowerOfTwo(end - 0x200000);

		state->external_ram.size = CC_COUNT_OF(state->external_ram.buffer);
		state->external_ram.non_volatile = (metadata & 0x4000) != 0;
		state->external_ram.data_size = (metadata >> 11) & 3;
		state->external_ram.device_type = (metadata >> 5) & 7;
		state->external_ram.mapped_in = clownmdemu->cartridge_buffer_length * sizeof(*clownmdemu->cartridge_buffer) <= 2 * 1024 * 1024; /* Cartridges larger than 2MiB need to map-in their external RAM explicitly. */
		/* TODO: Prevent small cartridges from mapping external RAM out. */

		if (metadata_junk_bits != 0xA000)
			LogMessage("External RAM metadata data at cartridge address 0x1B2 has incorrect junk bits - should be 0xA000, but was 0x%" CC_PRIXFAST16, metadata_junk_bits);

		if (state->external_ram.device_type != 1 && state->external_ram.device_type != 2)
			LogMessage("Invalid external RAM device type - should be 1 or 2, but was %" CC_PRIXLEAST8, state->external_ram.device_type);

		/* TODO: Add support for EEPROM. */
		if (state->external_ram.data_size == 1 || state->external_ram.device_type == 2)
			LogMessage("EEPROM external RAM is not yet supported - use SRAM instead");

		/* TODO: Should we just disable SRAM in these events? */
		/* TODO: SRAM should probably not be disabled in the first case, since the Sonic 1 disassembly makes this mistake by default. */
		if (state->external_ram.data_size != 3 && start != 0x200000)
		{
			LogMessage("Invalid external RAM start address - should be 0x200000, but was 0x%" CC_PRIXFAST32, start);
		}
		else if (state->external_ram.data_size == 3 && start != 0x200001)
		{
			LogMessage("Invalid external RAM start address - should be 0x200001, but was 0x%" CC_PRIXFAST32, start);
		}
		else if (end < start)
		{
			LogMessage("Invalid external RAM end address - should be after start address but was before it instead");
		}
		else if (size > CC_COUNT_OF(state->external_ram.buffer))
		{
			LogMessage("External RAM is too large - must be 0x%" CC_PRIXFAST32 " bytes or less, but was 0x%" CC_PRIXFAST32, (cc_u32f)CC_COUNT_OF(state->external_ram.buffer), size);
		}
		else
		{
			state->external_ram.size = size;
		}
	}
}

void ClownMDEmu_SetCartridge(ClownMDEmu* const clownmdemu, const cc_u16l* const buffer, const cc_u32f buffer_length)
{
	clownmdemu->cartridge_buffer = buffer;
	clownmdemu->cartridge_buffer_length = buffer_length;
}

void ClownMDEmu_Reset(const ClownMDEmu* const clownmdemu, const cc_bool cd_boot)
{
	ClownMDEmu_State* const state = clownmdemu->state;

	Clown68000_ReadWriteCallbacks m68k_read_write_callbacks;
	CPUCallbackUserData callback_user_data;

	SetUpExternalRAM(clownmdemu);

	state->mega_cd.boot_from_cd = cd_boot;

	if (cd_boot)
	{
		/* Boot from CD ("Mode 2"). */
		cc_u32f ip_start, ip_length, sp_start, sp_length;
		const cc_u16f boot_header_offset = 0x6000;
		const cc_u16f ip_start_default = 0x200;
		const cc_u16f ip_length_default = 0x600;
		cc_u16l* const sector_words = &state->mega_cd.prg_ram.buffer[boot_header_offset / 2];
		/*cc_u8l region;*/

		/* Read first sector. */
		clownmdemu->callbacks->cd_seeked((void*)clownmdemu->callbacks->user_data, 0);
		clownmdemu->callbacks->cd_sector_read((void*)clownmdemu->callbacks->user_data, sector_words); /* Sega's BIOS reads to PRG-RAM too. */
		ip_start = ClownMDEmu_U16sToU32(&sector_words[0x18]);
		ip_length = ClownMDEmu_U16sToU32(&sector_words[0x1A]);
		sp_start = ClownMDEmu_U16sToU32(&sector_words[0x20]);
		sp_length = ClownMDEmu_U16sToU32(&sector_words[0x22]);
		/*region = sector_bytes[0x1F0];*/

		/* Don't allow overflowing the PRG-RAM array. */
		sp_length = CC_MIN(CC_COUNT_OF(state->mega_cd.prg_ram.buffer) * 2 - boot_header_offset, sp_length);

		/* Read Initial Program. */
		memcpy(state->mega_cd.word_ram.buffer, &sector_words[ip_start_default / 2], ip_length_default);

		/* Load additional Initial Program data if necessary. */
		if (ip_start != ip_start_default || ip_length != ip_length_default)
			CDSectorsTo68kRAM(clownmdemu->callbacks, &state->mega_cd.word_ram.buffer[ip_length_default / 2], ip_start, 32 * CDC_SECTOR_SIZE);

		/* This is what Sega's BIOS does. */
		memcpy(state->m68k.ram, state->mega_cd.word_ram.buffer, sizeof(state->m68k.ram) / 2);

		/* Read Sub Program. */
		CDSectorsTo68kRAM(clownmdemu->callbacks, &state->mega_cd.prg_ram.buffer[boot_header_offset / 2], sp_start, sp_length);

		/* Give WORD-RAM to the SUB-CPU. */
		state->mega_cd.word_ram.dmna = cc_false;
		state->mega_cd.word_ram.ret = cc_false;
	}

	callback_user_data.clownmdemu = clownmdemu;

	m68k_read_write_callbacks.user_data = &callback_user_data;

	m68k_read_write_callbacks.read_callback = M68kReadCallback;
	m68k_read_write_callbacks.write_callback = M68kWriteCallback;
	Clown68000_Reset(clownmdemu->m68k, &m68k_read_write_callbacks);

	m68k_read_write_callbacks.read_callback = MCDM68kReadCallback;
	m68k_read_write_callbacks.write_callback = MCDM68kWriteCallback;
	Clown68000_Reset(clownmdemu->mcd_m68k, &m68k_read_write_callbacks);
}

void ClownMDEmu_SetLogCallback(const ClownMDEmu_LogCallback log_callback, const void* const user_data)
{
	SetLogCallback(log_callback, user_data);
	Clown68000_SetErrorCallback(log_callback, user_data);
}
