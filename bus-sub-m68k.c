/* TODO: */
/* https://bitbucket.org/eke/genesis-plus-gx/issues/29/mega-cd-support */
/* https://gendev.spritesmind.net/forum/viewtopic.php?t=3020 */
/* https://gendev.spritesmind.net/forum/viewtopic.php?p=18935#p18935 - The address is always masked with 0xFFFFF! */

#include "bus-sub-m68k.h"

#include <assert.h>

#include "bus-main-m68k.h"
#include "cdda.h"
#include "log.h"

static cc_u16f MCDM68kReadByte(const void* const user_data, const cc_u32f address, const CycleMegaCD target_cycle)
{
	const cc_bool is_odd = (address & 1) != 0;

	return (MCDM68kReadCallbackWithCycle(user_data, (address & 0xFFFFFF) / 2, !is_odd, is_odd, target_cycle) >> (is_odd ? 0 : 8)) & 0xFF;
}

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

static void MCDM68kWriteByte(const void* const user_data, const cc_u32f address, const cc_u16f value, const CycleMegaCD target_cycle)
{
	const cc_bool is_odd = (address & 1) != 0;

	MCDM68kWriteCallbackWithCycle(user_data, (address & 0xFFFFFF) / 2, !is_odd, is_odd, value << (is_odd ? 0 : 8), target_cycle);
}

static void MCDM68kWriteWord(const void* const user_data, const cc_u32f address, const cc_u16f value, const CycleMegaCD target_cycle)
{
	assert(address % 2 == 0);

	MCDM68kWriteCallbackWithCycle(user_data, (address & 0xFFFFFF) / 2, cc_true, cc_true, value, target_cycle);
}

static void ROMSEEK(const ClownMDEmu* const clownmdemu, const ClownMDEmu_Callbacks* const frontend_callbacks, const cc_u32f starting_sector, const cc_u32f total_sectors)
{
	CDC_Stop(&clownmdemu->state->mega_cd.cdc);
	CDC_Seek(&clownmdemu->state->mega_cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data, starting_sector, total_sectors);
	frontend_callbacks->cd_seeked((void*)frontend_callbacks->user_data, starting_sector);
}

static void CDCSTART(const ClownMDEmu* const clownmdemu, const ClownMDEmu_Callbacks* const frontend_callbacks)
{
	CDDA_SetPlaying(&clownmdemu->state->mega_cd.cdda, cc_false);
	CDC_Start(&clownmdemu->state->mega_cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data);
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
			CDDA_SetPaused(&clownmdemu->state->mega_cd.cdda, cc_true);
			break;

		case 0x04:
			/* MSCPAUSEOFF */
			CDDA_SetPaused(&clownmdemu->state->mega_cd.cdda, cc_false);
			break;

		case 0x11:
			/* MSCPLAY */
		case 0x12:
			/* MSCPLAY1 */
		case 0x13:
			/* MSCPLAYR */
		{
			const cc_u16f track_number = MCDM68kReadWord(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);

			CDDA_SetPlaying(&clownmdemu->state->mega_cd.cdda, cc_true);
			CDDA_SetPaused(&clownmdemu->state->mega_cd.cdda, cc_false);

			frontend_callbacks->cd_track_seeked((void*)frontend_callbacks->user_data, track_number, command == 0x11 ? CLOWNMDEMU_CDDA_PLAY_ALL : command == 0x12 ? CLOWNMDEMU_CDDA_PLAY_ONCE : CLOWNMDEMU_CDDA_PLAY_REPEAT);
			break;
		}

		case 0x17:
		{
			/* ROMREAD */
			const cc_u32f starting_sector = MCDM68kReadLongword(user_data, clownmdemu->mcd_m68k->address_registers[0] + 0, target_cycle);

			ROMSEEK(clownmdemu, frontend_callbacks, starting_sector, 0);
			CDCSTART(clownmdemu, frontend_callbacks);
			break;
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

		case 0x80:
			/* CDBCHK */
			clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that the BIOS is not busy. */
			break;

		case 0x83:
			/* CDBTOCREAD */
			/* TODO: Complete this! */
			clownmdemu->mcd_m68k->data_registers[0] = clownmdemu->mcd_m68k->data_registers[1] & 0xFF;
			clownmdemu->mcd_m68k->data_registers[1]	= 0xFF;
			break;

		case 0x85:
		{
			/* FDRSET */
			const cc_bool is_master_volume = (clownmdemu->mcd_m68k->data_registers[1] & 0x8000) != 0;
			const cc_u16f volume = clownmdemu->mcd_m68k->data_registers[1] & 0x7FFF;

			if (is_master_volume)
				CDDA_SetMasterVolume(&clownmdemu->state->mega_cd.cdda, volume);
			else
				CDDA_SetVolume(&clownmdemu->state->mega_cd.cdda, volume);

			break;
		}

		case 0x86:
		{
			/* FDRCHG */
			const cc_u16f target_volume = clownmdemu->mcd_m68k->data_registers[1] >> 16;
			const cc_u16f fade_step = clownmdemu->mcd_m68k->data_registers[1] & 0xFFFF;

			CDDA_FadeToVolume(&clownmdemu->state->mega_cd.cdda, target_volume, fade_step);

			break;
		}

		case 0x88:
			/* CDCSTART */
			CDCSTART(clownmdemu, frontend_callbacks);
			break;

		case 0x89:
			/* CDCSTOP */
			CDC_Stop(&clownmdemu->state->mega_cd.cdc);
			break;

		case 0x8A:
			/* CDCSTAT */
			if (!CDC_Stat(&clownmdemu->state->mega_cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data))
				clownmdemu->mcd_m68k->status_register |= 1; /* Set carry flag to signal that a sector is not ready. */
			else
				clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that there's a sector ready. */

			break;

		case 0x8B:
			/* CDCREAD */
			if (!CDC_Read(&clownmdemu->state->mega_cd.cdc, frontend_callbacks->cd_sector_read, frontend_callbacks->user_data, &clownmdemu->mcd_m68k->data_registers[0]))
			{
				/* Sonic Megamix 4.0b relies on this. */
				clownmdemu->mcd_m68k->status_register |= 1; /* Set carry flag to signal that a sector has not been prepared. */
			}
			else
			{
				/* TODO: This really belongs in the CDC logic, but it needs access to the RAM buffers... */
				switch (clownmdemu->state->mega_cd.cdc.device_destination)
				{
					case CDC_DESTINATION_PCM_RAM:
					case CDC_DESTINATION_PRG_RAM:
					case CDC_DESTINATION_WORD_RAM:
					{
						/* TODO: How is RAM address overflow handled? */
						cc_u32f address;
						const cc_u32f offset = (cc_u32f)clownmdemu->state->mega_cd.cdc.dma_address * 8;

						switch (clownmdemu->state->mega_cd.cdc.device_destination)
						{
							case 4:
								address = 0xFFFF2000 + (offset & 0x1FFF);
								break;

							case 5:
								address = 0 + (offset & 0x7FFFF);
								break;

							case 7:
								address = clownmdemu->state->mega_cd.word_ram.in_1m_mode ? 0xC0000 + (offset & 0x1FFFF) : 0x80000 + (offset & 0x3FFFF);
								break;
						}

						/* Discard the header data. */
						CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true);
						CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true);

						/* Copy the sector data to the DMA destination. */
						/* The behaviour of CDC-to-PCM DMA exposes that this really does leverage the Sub-CPU bus on a Mega CD:
						   the DMA destination address is measured in Sub-CPU address space bytes, not PCM RAM buffer bytes.
						   That is to say, setting it to 8 will cause the data to be copied to 4 bytes into PCM RAM. */
						while ((CDC_Mode(&clownmdemu->state->mega_cd.cdc, cc_true) & 0x4000) != 0)
						{
							const cc_u16f word = CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true);

							if (clownmdemu->state->mega_cd.cdc.device_destination == CDC_DESTINATION_PCM_RAM)
							{
								MCDM68kWriteWord(user_data, address, word >> 8, target_cycle);
								address += 2;
								MCDM68kWriteWord(user_data, address, word & 0xFF, target_cycle);
							}
							else
							{
								MCDM68kWriteWord(user_data, address, word, target_cycle);
							}

							address += 2;
						}

						break;
					}
				}

				clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that a sector has been prepared. */
			}

			break;

		case 0x8C:
			/* CDCTRN */
			if ((CDC_Mode(&clownmdemu->state->mega_cd.cdc, cc_true) & 0x8000) != 0)
			{
				clownmdemu->mcd_m68k->status_register |= 1; /* Set carry flag to signal that there's not a sector ready. */
			}
			else
			{
				cc_u32f i;
				const cc_u32f sector_address = clownmdemu->mcd_m68k->address_registers[0];
				const cc_u32f header_address = clownmdemu->mcd_m68k->address_registers[1];

				MCDM68kWriteWord(user_data, header_address + 0, CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true), target_cycle);
				MCDM68kWriteWord(user_data, header_address + 2, CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true), target_cycle);

				for (i = 0; i < CDC_SECTOR_SIZE; i += 2)
					MCDM68kWriteWord(user_data, sector_address + i, CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true), target_cycle);

				clownmdemu->mcd_m68k->address_registers[0] = (clownmdemu->mcd_m68k->address_registers[0] + CDC_SECTOR_SIZE) & 0xFFFFFFFF;
				clownmdemu->mcd_m68k->address_registers[1] = (clownmdemu->mcd_m68k->address_registers[1] + 4) & 0xFFFFFFFF;

				clownmdemu->mcd_m68k->status_register &= ~1; /* Clear carry flag to signal that there's always a sector ready. */
			}

			break;

		case 0x8D:
			/* CDCACK */
			CDC_Ack(&clownmdemu->state->mega_cd.cdc);
			break;

		default:
			LogMessage("UNRECOGNISED BIOS CALL DETECTED (0x%02" CC_PRIXFAST16 ")", command);
			break;
	}
}

void SyncMCDM68kForReal(const ClownMDEmu* const clownmdemu, const Clown68000_ReadWriteCallbacks* const m68k_read_write_callbacks, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const other_state = (CPUCallbackUserData*)m68k_read_write_callbacks->user_data;

	const cc_u32f current_cycle = other_state->sync.mcd_m68k.current_cycle;

	if (target_cycle.cycle > current_cycle)
	{
		if (clownmdemu->state->mega_cd.m68k.bus_requested || clownmdemu->state->mega_cd.m68k.reset_held)
		{
			other_state->sync.mcd_m68k.current_cycle = target_cycle.cycle / CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER * CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER;
		}
		else
		{
			const cc_u32f m68k_cycles_to_do = (target_cycle.cycle - current_cycle) / CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER;

			other_state->sync.mcd_m68k.base_cycle = current_cycle;
			other_state->sync.mcd_m68k.current_cycle = current_cycle + m68k_cycles_to_do * CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER;

			Clown68000_DoCycles(clownmdemu->mcd_m68k, m68k_read_write_callbacks, m68k_cycles_to_do);
		}
	}
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

static size_t StampMapDiameterInPixels(ClownMDEmu_State* const state)
{
	return state->mega_cd.rotation.large_stamp_map ? 1 << 12 : 1 << 8;
}

#define SHIFT_TO_NORMAL(SHIFT) ((size_t)1 << (SHIFT))
#define BITS_PER_PIXEL 4
#define BITS_PER_BYTE 8
#define BITS_PER_WORD 16
#define PIXELS_PER_BYTE (BITS_PER_BYTE / BITS_PER_PIXEL)
#define PIXELS_PER_WORD (BITS_PER_WORD / BITS_PER_PIXEL)

#define STAMP_TILE_DIAMETER_IN_PIXELS_SHIFT 3
#define STAMP_TILE_DIAMETER_IN_PIXELS SHIFT_TO_NORMAL(STAMP_TILE_DIAMETER_IN_PIXELS_SHIFT)

static cc_u8f StampDiameterInPixelsShift(ClownMDEmu_State* const state)
{
	return STAMP_TILE_DIAMETER_IN_PIXELS_SHIFT + 1 + state->mega_cd.rotation.large_stamp;
}

static const cc_u16l* GetStampMapAddress(ClownMDEmu_State* const state)
{
	return state->mega_cd.word_ram.buffer + (size_t)state->mega_cd.rotation.stamp_map_address * 2;
}

#define SMALL_STAMP_DIAMETER_IN_PIXELS_SHIFT (STAMP_TILE_DIAMETER_IN_PIXELS_SHIFT + 1) /* 16x16 */
#define SMALL_STAMP_DIAMETER_IN_PIXELS       SHIFT_TO_NORMAL(SMALL_STAMP_DIAMETER_IN_PIXELS_SHIFT)
#define LARGE_STAMP_DIAMETER_IN_PIXELS_SHIFT (STAMP_TILE_DIAMETER_IN_PIXELS_SHIFT + 2) /* 32x32 */
#define LARGE_STAMP_DIAMETER_IN_PIXELS       SHIFT_TO_NORMAL(LARGE_STAMP_DIAMETER_IN_PIXELS_SHIFT)

#define SMALL_STAMP_SIZE_IN_WORDS (SMALL_STAMP_DIAMETER_IN_PIXELS * SMALL_STAMP_DIAMETER_IN_PIXELS / PIXELS_PER_WORD)
#define LARGE_STAMP_SIZE_IN_WORDS (LARGE_STAMP_DIAMETER_IN_PIXELS * LARGE_STAMP_DIAMETER_IN_PIXELS / PIXELS_PER_WORD)

static const cc_u16l* GetStampAddress(ClownMDEmu_State* const state, const cc_u16f index)
{
	if (state->mega_cd.rotation.large_stamp)
		return state->mega_cd.word_ram.buffer + (index * SMALL_STAMP_SIZE_IN_WORDS);
	else
		return state->mega_cd.word_ram.buffer + (index / (LARGE_STAMP_SIZE_IN_WORDS / SMALL_STAMP_SIZE_IN_WORDS) * LARGE_STAMP_SIZE_IN_WORDS);
}

static size_t PixelIndexFromImageBufferCoordinate(const cc_u16f x, const cc_u16f y, const size_t image_buffer_height)
{
	const cc_u16f pixel_x_in_tile = x % STAMP_TILE_DIAMETER_IN_PIXELS;
	const cc_u8f tile_x_in_image_buffer = x / STAMP_TILE_DIAMETER_IN_PIXELS;

	const size_t tile_row_index = (size_t)tile_x_in_image_buffer * image_buffer_height + y;

	return (size_t)tile_row_index * STAMP_TILE_DIAMETER_IN_PIXELS + pixel_x_in_tile;
}

static cc_u8f ReadPixelFromWord(const cc_u16f word, const cc_u8f pixel_index)
{
	const cc_u8f pixel_mask = ((1 << BITS_PER_PIXEL) - 1);
	return (word >> (BITS_PER_WORD - (BITS_PER_PIXEL * (1 + pixel_index)))) & pixel_mask;
}

static void WritePixelToWord(cc_u16l* const word, const cc_u8f pixel_index, const cc_u8f pixel)
{
	const cc_u16f pixel_mask = ((1 << BITS_PER_PIXEL) - 1);
	const cc_u8f shift = (BITS_PER_WORD - (BITS_PER_PIXEL * (1 + pixel_index)));
	*word &= ~(pixel_mask << shift);
	*word |= (cc_u16f)pixel << shift;
}

static cc_u8f PixelFromStamp(const cc_u16l* const buffer, const cc_u16f x, const cc_u16f y, const size_t stamp_height)
{
	const cc_u32f pixel_index_within_stamp = PixelIndexFromImageBufferCoordinate(x, y, stamp_height);

	const cc_u32f word_index_within_stamp = pixel_index_within_stamp / PIXELS_PER_WORD;
	const cc_u8f pixel_index_within_word = pixel_index_within_stamp % PIXELS_PER_WORD;

	const cc_u16f word = buffer[word_index_within_stamp];
	const cc_u8f pixel = ReadPixelFromWord(word, pixel_index_within_word);

	return pixel;
}

static cc_u8f ReadPixelFromStampMap(ClownMDEmu_State* const state, const cc_u32f x, const cc_u32f y)
{
	const cc_u8f stamp_diameter_in_pixels_shift = StampDiameterInPixelsShift(state);
	const size_t stamp_diameter_in_pixels = SHIFT_TO_NORMAL(stamp_diameter_in_pixels_shift);
	const size_t stamp_map_diameter_in_pixels = StampMapDiameterInPixels(state);
	const size_t stamp_map_diameter_in_stamps = stamp_map_diameter_in_pixels >> stamp_diameter_in_pixels_shift;
	const size_t stamp_map_size_mask = stamp_map_diameter_in_pixels - 1;
	const size_t stamp_size_mask = stamp_diameter_in_pixels - 1;

	if (!state->mega_cd.rotation.repeating_stamp_map && (x >= stamp_map_diameter_in_pixels || y >= stamp_map_diameter_in_pixels))
	{
		return 0;
	}
	else
	{
		const size_t pixel_x_within_stamp_map = x & stamp_map_size_mask;
		const size_t pixel_y_within_stamp_map = y & stamp_map_size_mask;

		const size_t stamp_x_within_stamp_map = pixel_x_within_stamp_map >> stamp_diameter_in_pixels_shift;
		const size_t stamp_y_within_stamp_map = pixel_y_within_stamp_map >> stamp_diameter_in_pixels_shift;

		const size_t stamp_index_within_stamp_map = stamp_y_within_stamp_map * stamp_map_diameter_in_stamps + stamp_x_within_stamp_map;

		const cc_u16f stamp_metadata = GetStampMapAddress(state)[stamp_index_within_stamp_map];
		const cc_u16f stamp_index = stamp_metadata & 0x7FF;
		const cc_u8f rotation = (stamp_metadata >> 13) & 3;
		const cc_bool horizontal_flip = (stamp_metadata & 0x8000) != 0;

		if (stamp_index == 0)
		{
			return 0;
		}
		else
		{
			const cc_u16l* const stamp_address = GetStampAddress(state, stamp_index);

			cc_u16f pixel_x_within_stamp = pixel_x_within_stamp_map & stamp_size_mask;
			cc_u16f pixel_y_within_stamp = pixel_y_within_stamp_map & stamp_size_mask;

			cc_bool x_flip = cc_false, y_flip = cc_false, swap_coordinates = cc_false;

			switch (rotation)
			{
				case 0: /* 0 degrees */
					break;

				case 1: /* 90 degrees */
					y_flip = cc_true;
					swap_coordinates = cc_true;
					break;

				case 2: /* 180 degrees */
					x_flip = cc_true;
					y_flip = cc_true;
					break;

				case 3: /* 270 degrees */
					x_flip = cc_true;
					swap_coordinates = cc_true;
					break;
			}

			x_flip ^= horizontal_flip;

			if (x_flip)
				pixel_x_within_stamp = stamp_diameter_in_pixels - pixel_x_within_stamp - 1;

			if (y_flip)
				pixel_y_within_stamp = stamp_diameter_in_pixels - pixel_y_within_stamp - 1;

			return PixelFromStamp(stamp_address, swap_coordinates ? pixel_y_within_stamp : pixel_x_within_stamp, swap_coordinates ? pixel_x_within_stamp : pixel_y_within_stamp, stamp_diameter_in_pixels);
		}
	}
}

#define FILE_NAME_LENGTH 11
#define FILE_NAME_BUFFER_LENGTH (FILE_NAME_LENGTH + 1 + 2 + 1 + 3 + 1)

static void ReadFilename(const void* const user_data, char* const file_name_buffer, const cc_bool write_protected, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;

	char *file_name_pointer = file_name_buffer;
	cc_u8f i;

	for (i = 0; i < FILE_NAME_LENGTH; ++i)
		*file_name_pointer++ = MCDM68kReadByte(user_data, clownmdemu->mcd_m68k->address_registers[0] + i, target_cycle);

	if (write_protected)
	{
		*file_name_pointer++ = '.';
		*file_name_pointer++ = 'w';
		*file_name_pointer++ = 'p';
	}

	*file_name_pointer++ = '.';
	*file_name_pointer++ = 'b';
	*file_name_pointer++ = 'r';
	*file_name_pointer++ = 'm';

	*file_name_pointer++ = '\0';
}

static cc_bool OpenSaveFileForReading(const void* const user_data, const cc_bool write_protected, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	char file_name_buffer[FILE_NAME_BUFFER_LENGTH];

	ReadFilename(user_data, file_name_buffer, write_protected, target_cycle);
	return clownmdemu->callbacks->save_file_opened_for_reading((void*)clownmdemu->callbacks->user_data, file_name_buffer);
}

static cc_bool OpenSaveFileForReadingAny(const void* const user_data, cc_bool* const write_protected, const CycleMegaCD target_cycle)
{
	if (OpenSaveFileForReading(user_data, cc_false, target_cycle))
	{
		*write_protected = cc_false;
		return cc_true;
	}

	if (OpenSaveFileForReading(user_data, cc_true, target_cycle))
	{
		*write_protected = cc_true;
		return cc_true;
	}

	return cc_false;
}

static cc_bool OpenSaveFileForWriting(const void* const user_data, const cc_bool write_protected, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	char file_name_buffer[FILE_NAME_BUFFER_LENGTH];

	ReadFilename(user_data, file_name_buffer, write_protected, target_cycle);
	return clownmdemu->callbacks->save_file_opened_for_writing((void*)clownmdemu->callbacks->user_data, file_name_buffer);
}

static cc_bool RemoveSaveFile(const void* const user_data, const cc_bool write_protected, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	char file_name_buffer[FILE_NAME_BUFFER_LENGTH];

	ReadFilename(user_data, file_name_buffer, write_protected, target_cycle);
	return clownmdemu->callbacks->save_file_removed((void*)clownmdemu->callbacks->user_data, file_name_buffer);
}

static cc_bool RemoveSaveFileAny(const void* const user_data, const CycleMegaCD target_cycle)
{
	cc_bool success = cc_false;

	success |= RemoveSaveFile(user_data, cc_false, target_cycle);
	success |= RemoveSaveFile(user_data, cc_true, target_cycle);

	return success;
}

static cc_bool GetSaveFileSize(const void* const user_data, const cc_bool write_protected, size_t* const file_size, const CycleMegaCD target_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;
	const ClownMDEmu* const clownmdemu = callback_user_data->clownmdemu;
	char file_name_buffer[FILE_NAME_BUFFER_LENGTH];

	ReadFilename(user_data, file_name_buffer, write_protected, target_cycle);
	return clownmdemu->callbacks->save_file_size_obtained((void*)clownmdemu->callbacks->user_data, file_name_buffer, file_size);
}


static cc_bool GetSaveFileSizeAny(const void* const user_data, cc_bool* const write_protected, size_t* const file_size, const CycleMegaCD target_cycle)
{
	if (GetSaveFileSize(user_data, cc_false, file_size, target_cycle))
	{
		*write_protected = cc_false;
		return cc_true;
	}

	if (GetSaveFileSize(user_data, cc_true, file_size, target_cycle))
	{
		*write_protected = cc_true;
		return cc_true;
	}

	return cc_false;
}

#define BURAM_BLOCK_SIZE(WRITE_PROTECTED) ((WRITE_PROTECTED) ? 0x20 : 0x40)

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
					/* TODO: Report more files. */
					clownmdemu->mcd_m68k->data_registers[0] &= 0xFFFF0000;
					clownmdemu->mcd_m68k->data_registers[0] |= 100; /* 100 free blocks. */
					clownmdemu->mcd_m68k->data_registers[1] &= 0xFFFF0000;
					clownmdemu->mcd_m68k->data_registers[1] |= 1; /* Just one file. */
					break;

				case 0x02:
				{
					/* BRMSERCH */
					cc_bool write_protected;
					size_t file_size;

					if (!GetSaveFileSizeAny(user_data, &write_protected, &file_size, target_cycle))
					{
						clownmdemu->mcd_m68k->status_register |= 1; /* File not found. */
					}
					else
					{
						clownmdemu->mcd_m68k->data_registers[0] &= 0xFFFF0000;
						clownmdemu->mcd_m68k->data_registers[0] |= (file_size / BURAM_BLOCK_SIZE(write_protected)) & 0xFFFF;
						clownmdemu->mcd_m68k->data_registers[1] &= 0xFFFFFF00;
						clownmdemu->mcd_m68k->data_registers[1] |= write_protected ? 0xFF : 0;

						clownmdemu->mcd_m68k->status_register &= ~1; /* File found. */
					}

					break;
				}

				case 0x03:
				{
					/* BRMREAD */
					cc_bool write_protected;

					if (!OpenSaveFileForReadingAny(user_data, &write_protected, target_cycle))
					{
						clownmdemu->mcd_m68k->status_register |= 1; /* Error. */
					}
					else
					{
						cc_u32f total_bytes = 0;
						cc_s16f value;

						while ((value = frontend_callbacks->save_file_read((void*)frontend_callbacks->user_data)) != -1)
							MCDM68kWriteByte(user_data, clownmdemu->mcd_m68k->address_registers[1] + total_bytes++, value, target_cycle);

						frontend_callbacks->save_file_closed((void*)frontend_callbacks->user_data);

						clownmdemu->mcd_m68k->data_registers[0] &= 0xFFFF0000;
						clownmdemu->mcd_m68k->data_registers[0] |= (total_bytes / BURAM_BLOCK_SIZE(write_protected)) & 0xFFFF;
						clownmdemu->mcd_m68k->data_registers[1] &= 0xFFFFFF00;
						clownmdemu->mcd_m68k->data_registers[1] |= write_protected ? 0xFF : 0;

						clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					}

					break;
				}

				case 0x04:
				{
					/* BRMWRITE */
					const cc_bool write_protected = MCDM68kReadByte(user_data, clownmdemu->mcd_m68k->address_registers[0] + FILE_NAME_LENGTH, target_cycle) != 0;

					if (!OpenSaveFileForWriting(user_data, write_protected, target_cycle))
					{
						clownmdemu->mcd_m68k->status_register |= 1; /* Error. */
					}
					else
					{
						const cc_u16f total_blocks = MCDM68kReadWord(user_data, clownmdemu->mcd_m68k->address_registers[0] + FILE_NAME_LENGTH + 1, target_cycle);
						const cc_u32f total_bytes = (cc_u32f)total_blocks * BURAM_BLOCK_SIZE(write_protected);
						cc_u32f i;

						for (i = 0; i < total_bytes; ++i)
							frontend_callbacks->save_file_written((void*)frontend_callbacks->user_data, MCDM68kReadByte(user_data, clownmdemu->mcd_m68k->address_registers[1] + i, target_cycle));

						frontend_callbacks->save_file_closed((void*)frontend_callbacks->user_data);

						clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					}

					break;
				}

				case 0x05:
					/* BRMDEL */
					if (!RemoveSaveFileAny(user_data, target_cycle))
						clownmdemu->mcd_m68k->status_register |= 1; /* Error */
					else
						clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					break;

				case 0x06:
					/* BRMFORMAT */
					/* TODO: Delete everything? */
					clownmdemu->mcd_m68k->status_register &= ~1; /* Okay */
					break;

				case 0x07:
					/* BRMDIR */
					/* TODO: Implement this. */
					clownmdemu->mcd_m68k->status_register |= 1; /* Error. */
					break;

				case 0x08:
				{
					/* BRMVERIFY */
					const cc_bool write_protected = MCDM68kReadByte(user_data, clownmdemu->mcd_m68k->address_registers[0] + FILE_NAME_LENGTH, target_cycle) != 0;

					if (!OpenSaveFileForReading(user_data, write_protected, target_cycle))
					{
						clownmdemu->mcd_m68k->status_register |= 1; /* Error. */
					}
					else
					{
						/* TODO: Signal an error if the file is longer than expected? */
						const cc_u16f total_blocks = MCDM68kReadWord(user_data, clownmdemu->mcd_m68k->address_registers[0] + FILE_NAME_LENGTH + 1, target_cycle);
						const cc_u32f total_bytes = (cc_u32f)total_blocks * BURAM_BLOCK_SIZE(write_protected);
						cc_u32f i;

						for (i = 0; i < total_bytes; ++i)
						{
							const cc_u8f source_value = MCDM68kReadByte(user_data, clownmdemu->mcd_m68k->address_registers[1] + i, target_cycle);
							const cc_s16f destination_value = frontend_callbacks->save_file_read((void*)frontend_callbacks->user_data);

							/* End of file encountered too early, or mismatch. */
							if (destination_value == -1 || (cc_u16f)destination_value != source_value)
								break;
						}

						frontend_callbacks->save_file_closed((void*)frontend_callbacks->user_data);

						if (i != total_bytes)
							clownmdemu->mcd_m68k->status_register |= 1; /* Error. */
						else
							clownmdemu->mcd_m68k->status_register &= ~1; /* Okay. */
					}

					break;
				}

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
		else if (clownmdemu->state->mega_cd.word_ram.ret)
		{
			/* TODO: According to Page 24 of MEGA-CD HARDWARE MANUAL, this should cause the CPU to hang, just like the Z80 accessing the ROM during a DMA transfer. */
			LogMessage("SUB-CPU attempted to read from WORD-RAM while MAIN-CPU has it at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else
		{
			value = clownmdemu->state->mega_cd.word_ram.buffer[address_word % CC_COUNT_OF(clownmdemu->state->mega_cd.word_ram.buffer)];
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
			value = clownmdemu->state->mega_cd.word_ram.buffer[(address_word * 2 + !clownmdemu->state->mega_cd.word_ram.ret) % CC_COUNT_OF(clownmdemu->state->mega_cd.word_ram.buffer)];
		}
	}
	else if (address >= 0xFF0000 && address < 0xFF8000)
	{
		const cc_u16f masked_address = address_word & 0xFFF;

		if ((address & 0x2000) != 0)
		{
			/* PCM wave RAM */
			value = PCM_ReadWaveRAM(&clownmdemu->pcm, masked_address);
		}
		else
		{
			/* PCM register */
			SyncPCM(callback_user_data, target_cycle);
			value = PCM_ReadRegister(&clownmdemu->pcm, masked_address);
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
		value = ((cc_u16f)clownmdemu->state->mega_cd.prg_ram.write_protect << 8) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.in_1m_mode << 2) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.dmna << 1) | ((cc_u16f)clownmdemu->state->mega_cd.word_ram.ret << 0);
	}
	else if (address == 0xFF8004)
	{
		/* CDC mode / device destination */
		value = CDC_Mode(&clownmdemu->state->mega_cd.cdc, cc_true);
	}
	else if (address == 0xFF8006)
	{
		/* H-INT vector */
		LogMessage("SUB-CPU attempted to read from H-INT vector register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else if (address == 0xFF8008)
	{
		/* CDC host data */
		value = CDC_HostData(&clownmdemu->state->mega_cd.cdc, cc_true);
	}
	else if (address == 0xFF800A)
	{
		/* CDC DMA address */
		LogMessage("SUB-CPU attempted to read from DMA address register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
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
		value = clownmdemu->state->mega_cd.rotation.large_stamp_map << 2 | clownmdemu->state->mega_cd.rotation.large_stamp << 1 | clownmdemu->state->mega_cd.rotation.repeating_stamp_map << 0;
	}
	else if (address == 0xFF805A)
	{
		/* Stamp map base address */
		value = clownmdemu->state->mega_cd.rotation.stamp_map_address;
	}
	else if (address == 0xFF805C)
	{
		/* Image buffer vertical cell size */
		value = clownmdemu->state->mega_cd.rotation.image_buffer_height_in_tiles;
	}
	else if (address == 0xFF805E)
	{
		/* Image buffer base address */
		value = clownmdemu->state->mega_cd.rotation.image_buffer_address;
	}
	else if (address == 0xFF8060)
	{
		/* Image buffer offset */
		value = clownmdemu->state->mega_cd.rotation.image_buffer_y_offset << 3 | clownmdemu->state->mega_cd.rotation.image_buffer_x_offset << 0;
	}
	else if (address == 0xFF8062)
	{
		/* Image buffer width */
		value = clownmdemu->state->mega_cd.rotation.image_buffer_width;
	}
	else if (address == 0xFF8064)
	{
		/* Image buffer height */
		value = clownmdemu->state->mega_cd.rotation.image_buffer_height;
	}
	else if (address == 0xFF8066)
	{
		/* Trace table address */
		LogMessage("Attempted to read trace table address register at 0x%" CC_PRIXLEAST32, clownmdemu->mcd_m68k->program_counter);
	}
	else
	{
		LogMessage("Attempted to read invalid MCD 68k address 0x%" CC_PRIXFAST32 " at 0x%" CC_PRIXLEAST32, address, clownmdemu->mcd_m68k->program_counter);
	}

	return value;
}

cc_u16f MCDM68kReadCallback(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u32f current_cycle)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	return MCDM68kReadCallbackWithCycle(user_data, address, do_high_byte, do_low_byte, MakeCycleMegaCD(callback_user_data->sync.mcd_m68k.base_cycle + current_cycle * CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER));
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
		if (address < (cc_u32f)clownmdemu->state->mega_cd.prg_ram.write_protect * 0x200)
		{
			LogMessage("MAIN-CPU attempted to write to write-protected portion of PRG-RAM (0x%" CC_PRIXFAST32 ") at 0x%" CC_PRIXLEAST32, address, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else
		{
			clownmdemu->state->mega_cd.prg_ram.buffer[address_word] &= ~mask;
			clownmdemu->state->mega_cd.prg_ram.buffer[address_word] |= value & mask;
		}
	}
	else if (address < 0xC0000)
	{
		/* WORD-RAM */
		if (clownmdemu->state->mega_cd.word_ram.in_1m_mode)
		{
			/* TODO. */
			LogMessage("SUB-CPU attempted to write to the weird half of 1M WORD-RAM at 0x%" CC_PRIXLEAST32, clownmdemu->state->mega_cd.m68k.state.program_counter);
		}
		else if (clownmdemu->state->mega_cd.word_ram.ret)
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
		CDC_SetDeviceDestination(&clownmdemu->state->mega_cd.cdc, (CDC_DeviceDestination)(high_byte & 7));
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
	else if (address == 0xFF800A)
	{
		/* CDC DMA address */
		CDC_SetDMAAddress(&clownmdemu->state->mega_cd.cdc, value);
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
		clownmdemu->state->mega_cd.rotation.large_stamp_map = (value & (1 << 2)) != 0;
		clownmdemu->state->mega_cd.rotation.large_stamp = (value & (1 << 1)) != 0;
		clownmdemu->state->mega_cd.rotation.repeating_stamp_map = (value & (1 << 0)) != 0;
	}
	else if (address == 0xFF805A)
	{
		/* Stamp map base address */
		clownmdemu->state->mega_cd.rotation.stamp_map_address = value;
	}
	else if (address == 0xFF805C)
	{
		/* Image buffer vertical cell size */
		clownmdemu->state->mega_cd.rotation.image_buffer_height_in_tiles = value;
	}
	else if (address == 0xFF805E)
	{
		/* Image buffer base address */
		clownmdemu->state->mega_cd.rotation.image_buffer_address = value;
	}
	else if (address == 0xFF8060)
	{
		/* Image buffer offset */
		clownmdemu->state->mega_cd.rotation.image_buffer_y_offset = value >> 3 & 7;
		clownmdemu->state->mega_cd.rotation.image_buffer_x_offset = value >> 0 & 7;
	}
	else if (address == 0xFF8062)
	{
		/* Image buffer width */
		clownmdemu->state->mega_cd.rotation.image_buffer_width = value;
	}
	else if (address == 0xFF8064)
	{
		/* Image buffer height */
		/* TODO: Are the upper bits discarded or just left unused? */
		clownmdemu->state->mega_cd.rotation.image_buffer_height = value;
	}
	else if (address == 0xFF8066)
	{
		/* Trace table address */
		cc_u8f pixel_y_in_image_buffer;
		/* TODO: Correctly mask the address! */
		const cc_u16l *trace_table = &clownmdemu->state->mega_cd.word_ram.buffer[value * 2];
		const cc_u8f fraction_shift = 11;
		/* TODO: Does this actually offset the destination instead of the source? */
		const cc_u32f x_offset = -(cc_u32f)clownmdemu->state->mega_cd.rotation.image_buffer_x_offset << fraction_shift;
		const cc_u32f y_offset = -(cc_u32f)clownmdemu->state->mega_cd.rotation.image_buffer_y_offset << fraction_shift;

		/* TODO: Correctly mask the address! */
		cc_u16l* const image_buffer = &clownmdemu->state->mega_cd.word_ram.buffer[clownmdemu->state->mega_cd.rotation.image_buffer_address * 2];
		/* TODO: Rename 'image_buffer_height_in_tiles' to 'image_buffer_height_in_tiles_minus_one'. */
		const cc_u16f image_buffer_height_in_pixels = (clownmdemu->state->mega_cd.rotation.image_buffer_height_in_tiles + 1) * STAMP_TILE_DIAMETER_IN_PIXELS;

		for (pixel_y_in_image_buffer = 0; pixel_y_in_image_buffer < clownmdemu->state->mega_cd.rotation.image_buffer_height; ++pixel_y_in_image_buffer)
		{
			cc_u16f pixel_x_in_image_buffer;
			cc_u32f sample_x = x_offset + (CC_SIGN_EXTEND(cc_u32f, 15, trace_table[0]) << (fraction_shift - 3));
			cc_u32f sample_y = y_offset + (CC_SIGN_EXTEND(cc_u32f, 15, trace_table[1]) << (fraction_shift - 3));
			const cc_u32f delta_x = CC_SIGN_EXTEND(cc_u32f, 15, trace_table[2]);
			const cc_u32f delta_y = CC_SIGN_EXTEND(cc_u32f, 15, trace_table[3]);
			trace_table += 4;

			for (pixel_x_in_image_buffer = 0; pixel_x_in_image_buffer < clownmdemu->state->mega_cd.rotation.image_buffer_width; ++pixel_x_in_image_buffer)
			{
				const cc_u32f pixel_x = sample_x >> fraction_shift;
				const cc_u32f pixel_y = sample_y >> fraction_shift;
				const cc_u8f pixel = ReadPixelFromStampMap(clownmdemu->state, pixel_x, pixel_y);

				/* TODO: Priority mode! */
				const cc_u32f pixel_index_within_image_buffer = PixelIndexFromImageBufferCoordinate(pixel_x_in_image_buffer, pixel_y_in_image_buffer, image_buffer_height_in_pixels);

				const cc_u32f word_index_within_image_buffer = pixel_index_within_image_buffer / PIXELS_PER_WORD;
				const cc_u8f pixel_index_within_word = pixel_index_within_image_buffer % PIXELS_PER_WORD;

				cc_u16l* const word = &image_buffer[word_index_within_image_buffer];

				WritePixelToWord(word, pixel_index_within_word, pixel);

				sample_x += delta_x;
				sample_y += delta_y;
			}
		}

		/* The graphics operation decrements this until it reaches 0. Sonic CD relies on this to load its special stages. */
		clownmdemu->state->mega_cd.rotation.image_buffer_height = 0;

		/* Fire the 'graphics operation complete' interrupt. */
		if (clownmdemu->state->mega_cd.irq.enabled[0])
			clownmdemu->state->mega_cd.irq.irq1_pending = cc_true;
	}
	else
	{
		LogMessage("Attempted to write invalid MCD 68k address 0x%" CC_PRIXFAST32 " at 0x%" CC_PRIXLEAST32, address, clownmdemu->mcd_m68k->program_counter);
	}
}

void MCDM68kWriteCallback(const void* const user_data, const cc_u32f address, const cc_bool do_high_byte, const cc_bool do_low_byte, const cc_u32f current_cycle, const cc_u16f value)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	MCDM68kWriteCallbackWithCycle(user_data, address, do_high_byte, do_low_byte, value, MakeCycleMegaCD(callback_user_data->sync.mcd_m68k.base_cycle + current_cycle * CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER));
}
