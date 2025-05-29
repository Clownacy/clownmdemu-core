/* TODO: https://gendev.spritesmind.net/forum/viewtopic.php?p=21016#p21016 */
/* TODO: HV counter details - https://wiki.megadrive.org/index.php?title=VDP_Ports */

/* Some of the logic here is based on research done by Nemesis:
   https://gendev.spritesmind.net/forum/viewtopic.php?p=21016#p21016 */

#include "vdp.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "clowncommon/clowncommon.h"

#include "log.h"

#define TILE_WIDTH 8
#define TILE_PAIR_COUNT 2
#define TILE_PAIR_WIDTH (TILE_WIDTH * TILE_PAIR_COUNT)
#define SCANLINE_WIDTH_IN_TILE_PAIRS CC_DIVIDE_CEILING(VDP_MAX_SCANLINE_WIDTH, TILE_PAIR_WIDTH)
#define MAX_SPRITE_WIDTH (TILE_WIDTH * 4)

#define TILE_Y_INDEX_TO_TILE_BYTE_INDEX_SHIFT 2
#define TILE_Y_INDEX_TO_TILE_BYTE_INDEX(value) ((value) << TILE_Y_INDEX_TO_TILE_BYTE_INDEX_SHIFT)
#define GET_TILE_HEIGHT_SHIFT(state) (3 + (state)->double_resolution_enabled)
#define GET_TILE_HEIGHT_MASK(state) ((1 << GET_TILE_HEIGHT_SHIFT(state)) - 1)
#define MULTIPLY_BY_TILE_HEIGHT(state, value) ((value) << GET_TILE_HEIGHT_SHIFT(state))
#define DIVIDE_BY_TILE_HEIGHT(state, value) ((value) >> GET_TILE_HEIGHT_SHIFT(state))
#define GET_TILE_SIZE_SHIFT(state) (GET_TILE_HEIGHT_SHIFT(state) + TILE_Y_INDEX_TO_TILE_BYTE_INDEX_SHIFT)
#define MULTIPLY_BY_TILE_SIZE(state, value) ((value) << GET_TILE_SIZE_SHIFT(state))

#define READ_VRAM_WORD(STATE, ADDRESS) (ReadVRAM(STATE, (ADDRESS) ^ 0) | (ReadVRAM(STATE, (ADDRESS) ^ 1) << 8))

#define VRAM_ADDRESS_BASE_OFFSET(OPTION) ((OPTION) ? 0x10000 : 0)

enum
{
	SHADOW_HIGHLIGHT_NORMAL = 0 << 6,
	SHADOW_HIGHLIGHT_SHADOW = 1 << 6,
	SHADOW_HIGHLIGHT_HIGHLIGHT = 2 << 6
};

static cc_bool IsDMAPending(const VDP_State* const state)
{
	return (state->access.code_register & 0x20) != 0;
}

static void ClearDMAPending(VDP_State* const state)
{
	state->access.code_register &= ~0x20;
}

static cc_bool IsInReadMode(const VDP_State* const state)
{
	return (state->access.code_register & 1) == 0;
}

static void SetHScrollMode(VDP_State* const state, const VDP_HScrollMode mode)
{
	static const cc_u8l masks[4] = {0x00, 0x07, 0xF8, 0xFF};

	state->hscroll_mask = masks[(cc_u8f)mode];
}

static cc_u32f GetSpriteTableAddress(const VDP_State* const state)
{
	/* This masking is required for Titan Overdrive II's scene of the ship crash-landing to display the correct sprites. */
	return state->sprite_table_address & (state->h40_enabled ? ~(cc_u32f)0x3FF : ~(cc_u32f)0x1FF);
}

static cc_u32f DecodeVRAMAddress(const VDP_State* const state, const cc_u32f address)
{
	/* TODO: Master System mode. */

	if (state->extended_vram_enabled)
	{
		/* 128KiB mode. Slower, but this mode is rarely used anyway. */
		return ((address & 0x1F802) >> 1) | ((address & 0x400) >> 9) | (address & 0x3FC) | ((address & 1) << 16);
	}
	else
	{
		/* 64KiB mode. Left mostly as-is to maximise performance of most Mega Drive software. */
		return address & 0xFFFF;
	}
}

static cc_u8f ReadVRAM(const VDP_State* const state, const cc_u32f address)
{
	/* This masking ensures that the same byte is read for both halves of a word in 128KiB mode, just like a real Mega Drive does. */
	return state->vram[DecodeVRAMAddress(state, address) % CC_COUNT_OF(state->vram)];
}

static void WriteVRAM(VDP_State* const state, const cc_u32f address, const cc_u8f value)
{
	const cc_u32f decoded_address = DecodeVRAMAddress(state, address);

	/* Update sprite cache if we're writing to the sprite table */
	/* TODO: Do DMA fills and copies do this? */
	const cc_u32f sprite_table_index = address - GetSpriteTableAddress(state);

	if (sprite_table_index < (state->h40_enabled ? 80u : 64u) * 8u && (sprite_table_index & 4) == 0)
	{
		cc_u8l* const cache_bytes = state->sprite_table_cache[sprite_table_index / 8];

		cache_bytes[sprite_table_index & 3] = value;

		state->sprite_row_cache.needs_updating = cc_true;
	}

	/* Only write data that is within the first 64KiB bank, since a real Mega Drive is missing a second 64KiB VRAM chip. */
	if (decoded_address < CC_COUNT_OF(state->vram))
		state->vram[decoded_address] = value;
}

static void IncrementAddressRegister(VDP_State* const state)
{
	state->access.address_register += state->access.increment;
	state->access.address_register &= 0x1FFFF; /* Needs to be able to address 128KiB. */
}

static void WriteAndIncrement(VDP_State* const state, const cc_u16f value, const VDP_ColourUpdatedCallback colour_updated_callback, const void* const colour_updated_callback_user_data)
{
	switch (state->access.selected_buffer)
	{
		case VDP_ACCESS_VRAM:
			WriteVRAM(state, state->access.address_register ^ 0, (cc_u8f)(value & 0xFF));
			WriteVRAM(state, state->access.address_register ^ 1, (cc_u8f)(value >> 8));
			break;

		case VDP_ACCESS_CRAM:
		{
			/* Remove garbage bits */
			const cc_u16f colour = value & 0xEEE;

			/* Fit index to within CRAM */
			const cc_u16f index_wrapped = (state->access.address_register / 2) % CC_COUNT_OF(state->cram);

			/* Store regular Mega Drive-format colour (with garbage bits intact) */
			state->cram[index_wrapped] = colour;

			/* Now let's precompute the shadow/normal/highlight colours in
			   RGB444 (so we don't have to calculate them during blitting)
			   and send them to the frontend for further optimisation */

			/* Create normal colour */
			/* (repeat the upper bit in the lower bit so that the full 4-bit colour range is covered) */
			colour_updated_callback((void*)colour_updated_callback_user_data, SHADOW_HIGHLIGHT_NORMAL + index_wrapped, colour | ((colour & 0x888) >> 3));

			/* Create shadow colour */
			/* (divide by two and leave in lower half of colour range) */
			colour_updated_callback((void*)colour_updated_callback_user_data, SHADOW_HIGHLIGHT_SHADOW + index_wrapped, colour >> 1);

			/* Create highlight colour */
			/* (divide by two and move to upper half of colour range) */
			colour_updated_callback((void*)colour_updated_callback_user_data, SHADOW_HIGHLIGHT_HIGHLIGHT + index_wrapped, 0x888 + (colour >> 1));

			break;
		}

		case VDP_ACCESS_VSRAM:
		{
			const cc_u8f limit = 40;
			const cc_u8f index_wrapped = (state->access.address_register / 2) % CC_COUNT_OF(state->vsram);

			/* VRAM only holds 40 valid words, so discard the remaining 14. */
			if (index_wrapped < limit)
			{
				const cc_u16l vscroll = (cc_u16l)(value & 0x7FF);

				/* The first two values are mirrored in the 'empty' 14 words. */
				if (index_wrapped < 2)
				{
					cc_u8f i;
					for (i = limit + index_wrapped; i < CC_COUNT_OF(state->vsram); i += 2)
						state->vsram[i] = vscroll;
				}

				state->vsram[index_wrapped] = vscroll;
			}

			break;
		}

		default:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case VDP_ACCESS_INVALID:
		case VDP_ACCESS_VRAM_8BIT:
			LogMessage("VDP write attempted with invalid access mode specified (0x%" CC_PRIXFAST16 ")", state->access.code_register);
			break;
	}

	IncrementAddressRegister(state);
}

static cc_u16f ReadAndIncrement(VDP_State* const state)
{
	const cc_u16f word_address = state->access.address_register / 2;

	/* Oddly, leftover data from the FIFO resides in the unused bits. */
	/* Validated with Nemesis' 'VDPFIFOTesting' homebrew. */
	cc_u16f value = state->previous_data_writes[0];

	switch (state->access.selected_buffer)
	{
		case VDP_ACCESS_VRAM:
			value = READ_VRAM_WORD(state, word_address * 2);
			break;

		case VDP_ACCESS_CRAM:
			value &= ~0xEEE;
			value |= state->cram[word_address % CC_COUNT_OF(state->cram)];
			break;

		case VDP_ACCESS_VSRAM:
			value &= ~0x7FF;
			value |= state->vsram[word_address % CC_COUNT_OF(state->vsram)];
			break;

		case VDP_ACCESS_VRAM_8BIT:
			value &= ~0xFF;
			value |= ReadVRAM(state, state->access.address_register);
			break;

		default:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case VDP_ACCESS_INVALID:
			LogMessage("VDP read attempted with invalid access mode specified (0x%" CC_PRIXFAST16 ")", state->access.code_register);
			break;
	}

	IncrementAddressRegister(state);

	return value;
}

void VDP_Constant_Initialise(VDP_Constant* const constant)
{
	/* This essentially pre-computes the VDP's depth-test and alpha-test,
	   generating a lookup table to eliminate the need to perform these
	   every time a pixel is blitted. This provides a *massive* speed boost. */
	cc_u16f new_pixel_high;

	for (new_pixel_high = 0; new_pixel_high < CC_COUNT_OF(constant->blit_lookup.normal); ++new_pixel_high)
	{
		cc_u16f old_pixel;

		for (old_pixel = 0; old_pixel < CC_COUNT_OF(constant->blit_lookup.normal[0]); ++old_pixel)
		{
			cc_u16f new_pixel_low;

			for (new_pixel_low = 0; new_pixel_low < CC_COUNT_OF(constant->blit_lookup.normal[0][0]); ++new_pixel_low)
			{
				const cc_u16f palette_line_index_mask = 0xF;
				const cc_u16f colour_index_mask = 0x3F;
				const cc_u16f priority_mask = 0x40;
				const cc_u16f not_shadowed_mask = 0x80;

				const cc_u16f old_palette_line_index = old_pixel & palette_line_index_mask;
				const cc_u16f old_colour_index = old_pixel & colour_index_mask;
				const cc_bool old_priority = (old_pixel & priority_mask) != 0;
				const cc_bool old_not_shadowed = (old_pixel & not_shadowed_mask) != 0;

				const cc_u16f new_pixel = (new_pixel_high << 4) | new_pixel_low;

				const cc_u16f new_palette_line_index = new_pixel & palette_line_index_mask;
				const cc_u16f new_colour_index = new_pixel & colour_index_mask;
				const cc_bool new_priority = (new_pixel & priority_mask) != 0;
				const cc_bool new_not_shadowed = new_priority;

				const cc_bool draw_new_pixel = new_palette_line_index != 0 && (old_palette_line_index == 0 || !old_priority || new_priority);

				cc_u16f output;

				/* First, generate the table for regular blitting */
				output = draw_new_pixel ? new_pixel : old_pixel;

				output |= old_not_shadowed || new_not_shadowed ? not_shadowed_mask : 0;

				constant->blit_lookup.normal[new_pixel_high][old_pixel][new_pixel_low] = (cc_u8l)output;

				/* Now, generate the table for shadow/highlight blitting */
				if (draw_new_pixel)
				{
					/* Sprite goes on top of plane */
					if (new_colour_index == 0x3E)
					{
						/* Transparent highlight pixel */
						output = old_colour_index | (old_not_shadowed ? SHADOW_HIGHLIGHT_HIGHLIGHT : SHADOW_HIGHLIGHT_NORMAL);
					}
					else if (new_colour_index == 0x3F)
					{
						/* Transparent shadow pixel */
						output = old_colour_index | SHADOW_HIGHLIGHT_SHADOW;
					}
					else if (new_palette_line_index == 0xE)
					{
						/* Always-normal pixel */
						output = new_colour_index | SHADOW_HIGHLIGHT_NORMAL;
					}
					else
					{
						/* Regular sprite pixel */
						output = new_colour_index | (new_not_shadowed || old_not_shadowed ? SHADOW_HIGHLIGHT_NORMAL : SHADOW_HIGHLIGHT_SHADOW);
					}
				}
				else
				{
					/* Plane goes on top of sprite */
					output = old_colour_index | (old_not_shadowed ? SHADOW_HIGHLIGHT_NORMAL : SHADOW_HIGHLIGHT_SHADOW);
				}

				constant->blit_lookup.shadow_highlight[new_pixel_high][old_pixel][new_pixel_low] = (cc_u8l)output;

				/* Finally, generate AND lookup table, for the debug register. */
				constant->blit_lookup.forced_layer[new_pixel_high][old_pixel][new_pixel_low] = (cc_u8l)(old_pixel & (new_colour_index | ~colour_index_mask));
			}
		}
	}
}

void VDP_State_Initialise(VDP_State* const state)
{
	state->access.write_pending = cc_false;
	state->access.address_register = 0;
	state->access.code_register = 0;
	state->access.selected_buffer = VDP_ACCESS_VRAM;
	state->access.increment = 0;

	state->dma.enabled = cc_false;
	state->dma.mode = VDP_DMA_MODE_MEMORY_TO_VRAM;
	state->dma.source_address_high = 0;
	state->dma.source_address_low = 0;
	state->dma.length = 0;

	state->plane_a_address = 0;
	state->plane_b_address = 0;
	state->window_address = 0;
	state->sprite_table_address = 0;
	state->hscroll_address = 0;

	state->window.aligned_right = cc_false;
	state->window.aligned_bottom = cc_false;
	state->window.horizontal_boundary = 0;
	state->window.vertical_boundary = 0;

	state->plane_width_shift = 5;
	state->plane_height_bitmask = 0x1F;

	state->extended_vram_enabled = cc_false;
	state->display_enabled = cc_false;
	state->v_int_enabled = cc_false;
	state->h_int_enabled = cc_false;
	state->h40_enabled = cc_false;
	state->v30_enabled = cc_false;
	state->mega_drive_mode_enabled = cc_false;
	state->shadow_highlight_enabled = cc_false;
	state->double_resolution_enabled = cc_false;
	state->sprite_tile_index_rebase = cc_false;
	state->plane_a_tile_index_rebase = cc_false;
	state->plane_b_tile_index_rebase = cc_false;

	state->background_colour = 0;
	state->h_int_interval = 0;
	state->currently_in_vblank = cc_false;
	state->allow_sprite_masking = cc_false;

	SetHScrollMode(state, VDP_HSCROLL_MODE_FULL);
	state->vscroll_mode = VDP_VSCROLL_MODE_FULL;

	state->debug.forced_layer = 0;

	memset(state->vram, 0, sizeof(state->vram));
	memset(state->cram, 0, sizeof(state->cram));
	memset(state->vsram, 0, sizeof(state->vsram));
	memset(state->sprite_table_cache, 0, sizeof(state->sprite_table_cache));

	state->sprite_row_cache.needs_updating = cc_true;
	memset(state->sprite_row_cache.rows, 0, sizeof(state->sprite_row_cache.rows));

	memset(state->previous_data_writes, 0, sizeof(state->previous_data_writes));

	state->kdebug_buffer_index = 0;
	/* This byte never gets overwritten, so we can set it ahead of time. */
	state->kdebug_buffer[CC_COUNT_OF(state->kdebug_buffer) - 1] = '\0';
}

static cc_u16f GetHScrollTableOffset(const VDP_State* const state, const cc_u16f scanline)
{
	return ((scanline >> state->double_resolution_enabled) & state->hscroll_mask) * 4;
}

static cc_u16f GetVScrollTableOffset(const VDP_State* const state, const cc_u8f tile_pair)
{
	switch (state->vscroll_mode)
	{
		default:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case VDP_VSCROLL_MODE_FULL:
			return 0;

		case VDP_VSCROLL_MODE_2CELL:
			return (tile_pair * 2) % CC_COUNT_OF(state->vsram);
	}
}

static void RenderTilePair(const VDP* const vdp, const cc_u16f pixel_y_in_plane, const cc_u32f vram_address, const cc_u32f base_tile_vram_address, cc_u8l** const metapixels_pointer, const VDP_BlitLookupLower* const blit_lookup_list)
{
	const VDP_State* const state = vdp->state;
	const cc_u8l* const vram = state->vram;

	const cc_u8f tile_height_shift = GET_TILE_HEIGHT_SHIFT(state);
	const cc_u8f tile_height_mask = (1 << tile_height_shift) - 1;
	const cc_u8f pixel_y_in_tile_unflipped = pixel_y_in_plane & tile_height_mask;

	cc_u8f i;

	for (i = 0; i < TILE_PAIR_COUNT; ++i)
	{
		const cc_u16f word_vram_address = vram_address + i * 2;
		const cc_u16f word = READ_VRAM_WORD(state, word_vram_address);
		const cc_u8f x_flip = -(cc_u8f)VDP_GetTileXFlip(word);
		const cc_u8f y_flip = -(cc_u8f)VDP_GetTileYFlip(word);

		/* Get the Y coordinate of the pixel in the tile */
		const cc_u8f pixel_y_in_tile = pixel_y_in_tile_unflipped ^ (tile_height_mask & y_flip);

		/* Get raw tile data that contains the desired metapixel */
		const cc_u32f tile_row_vram_address = base_tile_vram_address + TILE_Y_INDEX_TO_TILE_BYTE_INDEX((VDP_GetTileIndex(word) << tile_height_shift) + pixel_y_in_tile);

		const cc_u8f byte_index_xor = 1 ^ 3 & x_flip;
		const cc_u8f nybble_shift_2 = 4 & x_flip;
		const cc_u8f nybble_shift_1 = 4 ^ nybble_shift_2;

		const VDP_BlitLookupNybble* const blit_lookup = blit_lookup_list[(word >> 13) & 7];

		cc_u8f j;

		for (j = 0; j < TILE_WIDTH / 2; ++j)
		{
			const cc_u8f byte = ReadVRAM(state, tile_row_vram_address + j ^ byte_index_xor);

			**metapixels_pointer = blit_lookup[**metapixels_pointer][(byte >> nybble_shift_1) & 0xF];
			++*metapixels_pointer;
			**metapixels_pointer = blit_lookup[**metapixels_pointer][(byte >> nybble_shift_2) & 0xF];
			++*metapixels_pointer;
		}
	}
}

static void RenderScrollingPlane(const VDP* const vdp, const cc_u8f start, const cc_u8f end, const cc_u16f scanline, const cc_u8f plane_index, const cc_u16f plane_x_offset, cc_u8l* const metapixels, const VDP_BlitLookupLower* const blit_lookup_list)
{
	VDP_State* const state = vdp->state;

	const cc_u32f base_tile_vram_address = VRAM_ADDRESS_BASE_OFFSET(plane_index == 0 ? state->plane_a_tile_index_rebase : state->plane_b_tile_index_rebase);
	const cc_u16f plane_pitch_shift = state->plane_width_shift;
	const cc_u16f plane_width_bitmask = (1 << plane_pitch_shift) - 1;
	const cc_u16f plane_height_bitmask = state->plane_height_bitmask;
	const cc_u32f plane_address = plane_index == 0 ? state->plane_a_address : state->plane_b_address;

	const cc_u16f tile_height_shift = GET_TILE_HEIGHT_SHIFT(state);

	cc_u8l *metapixels_pointer = &metapixels[start * TILE_PAIR_WIDTH];

	cc_u8f i;

	/* Note that we do an extra tile here. */
	for (i = start; i <= end && i < SCANLINE_WIDTH_IN_TILE_PAIRS + 1; ++i)
	{
		/* The '-1' here causes the first tile pair V-scroll value to be invalid, recreating a behaviour that occurs on real Mega Drives. */
		const cc_u16f vscroll = state->vsram[plane_index + GetVScrollTableOffset(state, i - 1)];

		/* Get the Y coordinate of the pixel in the plane */
		const cc_u16f pixel_y_in_plane = vscroll + scanline;

		/* This recreates the behaviour where the wrong tiles are drawn to the right of the window
		   plane when Plane A is scrolled horizontally by an amount that is not a multiple of 16. */
		/* Unsigned integer underflow prevents this behaviour from occurring when the window plane is not visible. */
		const cc_u16f clamped_i = CC_MAX(start, i - 1);

		/* Get the coordinates of the tile in the plane */
		const cc_u16f tile_x = ((plane_x_offset + clamped_i) * 2) & plane_width_bitmask;
		const cc_u16f tile_y = (pixel_y_in_plane >> tile_height_shift) & plane_height_bitmask;
		const cc_u32f vram_address = plane_address + ((tile_y << plane_pitch_shift) + tile_x) * 2;

		RenderTilePair(vdp, pixel_y_in_plane, vram_address, base_tile_vram_address, &metapixels_pointer, blit_lookup_list);
	}
}

static void RenderWindowPlane(const VDP* const vdp, const cc_u8f start, const cc_u8f end, const cc_u16f scanline, cc_u8l* const metapixels, const VDP_BlitLookupLower* const blit_lookup_list)
{
	const VDP_State* const state = vdp->state;

	const cc_u32f base_tile_vram_address = VRAM_ADDRESS_BASE_OFFSET(state->plane_a_tile_index_rebase);
	const cc_u16f tile_y = DIVIDE_BY_TILE_HEIGHT(state, scanline);
	const cc_u8f plane_pitch_shift = 5 + state->h40_enabled;

	cc_u8l *metapixels_pointer = &metapixels[start * TILE_PAIR_WIDTH];
	cc_u32f vram_address = state->window_address + ((tile_y << plane_pitch_shift) + start * TILE_PAIR_COUNT) * 2;

	cc_u8f i;

	/* Render tiles */
	for (i = start; i < end && i < SCANLINE_WIDTH_IN_TILE_PAIRS; ++i)
	{
		RenderTilePair(vdp, scanline, vram_address, base_tile_vram_address, &metapixels_pointer, blit_lookup_list);
		vram_address += 4;
	}
}

static void UpdateSpriteCache(VDP_State* const state)
{
	/* Caching and preprocessing some of the sprite table allows the renderer to avoid
	   scanning the entire sprite table every time it renders a scanline. The VDP actually
	   partially caches its sprite data too, though I don't know if it's for the same purpose. */
	const cc_u8f tile_height_shift = GET_TILE_HEIGHT_SHIFT(state);
	const cc_u8f max_sprites = state->h40_enabled ? 80 : 64;

	cc_u16f i;
	cc_u8f sprite_index;
	cc_u8f sprites_remaining = max_sprites;

	if (!state->sprite_row_cache.needs_updating)
		return;

	state->sprite_row_cache.needs_updating = cc_false;

	/* Make it so we write to the start of the rows */
	for (i = 0; i < CC_COUNT_OF(state->sprite_row_cache.rows); ++i)
		state->sprite_row_cache.rows[i].total = 0;

	sprite_index = 0;

	do
	{
		const VDP_CachedSprite cached_sprite = VDP_GetCachedSprite(state, sprite_index);
		const cc_u16f blank_lines = 128 << state->double_resolution_enabled;

		/* This loop only processes rows that are on-screen. */
		for (i = CC_MAX(blank_lines, cached_sprite.y); i < CC_MIN(blank_lines + ((state->v30_enabled ? 30 : 28) << tile_height_shift), cached_sprite.y + (cached_sprite.height << tile_height_shift)); ++i)
		{
			struct VDP_SpriteRowCacheRow* const row = &state->sprite_row_cache.rows[i - blank_lines];

			/* Don't write more sprites than are allowed to be drawn on this line */
			if (row->total != (state->h40_enabled ? 20 : 16))
			{
				struct VDP_SpriteRowCacheEntry* const sprite_row_cache_entry = &row->sprites[row->total++];

				sprite_row_cache_entry->table_index = (cc_u8l)sprite_index;
				sprite_row_cache_entry->width = (cc_u8l)cached_sprite.width;
				sprite_row_cache_entry->height = (cc_u8l)cached_sprite.height;
				sprite_row_cache_entry->y_in_sprite = (cc_u8l)(i - cached_sprite.y);
			}
		}

		if (cached_sprite.link >= max_sprites)
		{
			/* Invalid link - bail before it can cause a crash.
			   According to Nemesis, this is actually what real hardware does too:
			   http://gendev.spritesmind.net/forum/viewtopic.php?p=8364#p8364 */
			break;
		}

		sprite_index = cached_sprite.link;
	}
	while (sprite_index != 0 && --sprites_remaining != 0);
}

static void RenderSprites(cc_u8l (* const sprite_metapixels)[2], VDP_State* const state, const cc_u16f scanline)
{
	const cc_u32f base_tile_vram_address = VRAM_ADDRESS_BASE_OFFSET(state->sprite_tile_index_rebase);
	const cc_u8f tile_height_shift = GET_TILE_HEIGHT_SHIFT(state);
	const cc_u8f tile_height_mask = GET_TILE_HEIGHT_MASK(state);

	cc_u8f i;
	cc_u16f sprite_limit = state->h40_enabled ? 20 : 16;
	cc_u16f pixel_limit = state->h40_enabled ? 320 : 256;
	cc_bool masked = cc_false;

	/* Render sprites */
	/* This has been verified with Nemesis's sprite masking and overflow test ROM:
	   https://segaretro.org/Sprite_Masking_and_Overflow_Test_ROM */
	for (i = 0; i < state->sprite_row_cache.rows[scanline].total; ++i)
	{
		struct VDP_SpriteRowCacheEntry* const sprite_row_cache_entry = &state->sprite_row_cache.rows[scanline].sprites[i];

		/* Decode sprite data */
		const cc_u32f sprite_index = GetSpriteTableAddress(state) + sprite_row_cache_entry->table_index * 8;
		const cc_u16f width = sprite_row_cache_entry->width;
		const cc_u16f x = READ_VRAM_WORD(state, sprite_index + 6) & 0x1FF;

		/* This is a masking sprite: prevent all remaining sprites from being drawn */
		if (x == 0)
			masked = state->allow_sprite_masking;
		else
			/* Enable sprite masking after successfully drawing a sprite. */
			state->allow_sprite_masking = cc_true;

		/* Skip rendering when possible or required. */
		if (masked || x + width * TILE_WIDTH <= 0x80u || x >= 0x80u + (state->h40_enabled ? 40 : 32) * TILE_WIDTH)
		{
			if (pixel_limit <= width * TILE_WIDTH)
				return;

			pixel_limit -= width * TILE_WIDTH;
		}
		else
		{
			const cc_u16f height = sprite_row_cache_entry->height;
			const cc_u16f word = READ_VRAM_WORD(state, sprite_index + 4);
			const cc_u16f sprite_tile_index = VDP_GetTileIndex(word);
			const cc_bool x_flip = VDP_GetTileXFlip(word);
			const cc_bool y_flip = VDP_GetTileYFlip(word);

			const cc_u8f metapixel_high_bits = (word >> 13) & 7;

			const cc_u8f byte_index_xor = 1 ^ (x_flip ? 3 : 0);

			const cc_u8f y_in_sprite_non_flipped = sprite_row_cache_entry->y_in_sprite;
			const cc_u8f y_in_sprite = y_flip ? (height << tile_height_shift) - y_in_sprite_non_flipped - 1 : y_in_sprite_non_flipped;
			const cc_u16f pixel_y_in_tile = y_in_sprite & tile_height_mask;

			cc_u8l *metapixels_pointer = sprite_metapixels[(MAX_SPRITE_WIDTH - 1) + x - 0x80];

			cc_u8f nybble_shift[2];
			cc_u16f j;

			if (x_flip)
			{
				nybble_shift[0] = 0;
				nybble_shift[1] = 4;
			}
			else
			{
				nybble_shift[0] = 4;
				nybble_shift[1] = 0;
			}

			for (j = 0; j < width; ++j)
			{
				const cc_u16f x_in_sprite = x_flip ? width - j - 1 : j;
				const cc_u16f tile_index = sprite_tile_index + (y_in_sprite >> tile_height_shift) + x_in_sprite * height; /* TODO: Somehow get rid of this multiplication for a speed boost on platforms with a slow multiplier. */

				/* Get raw tile data that contains the desired metapixel */
				const cc_u32f tile_row_vram_address = base_tile_vram_address + TILE_Y_INDEX_TO_TILE_BYTE_INDEX(MULTIPLY_BY_TILE_HEIGHT(state, tile_index) + pixel_y_in_tile);

				cc_u16f k;

				for (k = 0; k < TILE_WIDTH / 2; ++k)
				{
					const cc_u8f byte = ReadVRAM(state, tile_row_vram_address + k ^ byte_index_xor);

					cc_u8f l;

					for (l = 0; l < CC_COUNT_OF(nybble_shift); ++l)
					{
						const cc_u8f palette_line_index = (byte >> nybble_shift[l]) & 0xF;

						const cc_u8f mask = 0 - (cc_u8f)((*metapixels_pointer == 0) & (palette_line_index != 0));

						*metapixels_pointer++ |= palette_line_index & mask;
						*metapixels_pointer++ |= metapixel_high_bits & mask;

						if (--pixel_limit == 0)
							return;
					}
				}
			}
		}

		if (--sprite_limit == 0)
			break;
	}

	/* Prevent sprite masking when ending a scanline without reaching the pixel limit. */
	state->allow_sprite_masking = cc_false;
}

static void RenderScrollPlane(const VDP* const vdp, const cc_u8f left_boundary, const cc_u8f right_boundary, const cc_u16f scanline, cc_u8l* const plane_metapixels, const VDP_BlitLookupLower* const blit_lookup_list, const cc_u8f plane_index)
{
	VDP_State* const state = vdp->state;

	if (!vdp->configuration->planes_disabled[plane_index])
	{
		const cc_u32f hscroll_vram_address = state->hscroll_address + plane_index * 2 + GetHScrollTableOffset(state, scanline);
		const cc_u16f hscroll = READ_VRAM_WORD(state, hscroll_vram_address);

		/* Get the value used to offset the writes to the metapixel buffer */
		const cc_u16f scroll_offset = TILE_PAIR_WIDTH - (hscroll % TILE_PAIR_WIDTH);

		/* Get the value used to offset the reads from the plane map */
		const cc_u16f plane_x_offset = -(hscroll / TILE_PAIR_WIDTH);

		RenderScrollingPlane(vdp, left_boundary, right_boundary, scanline, plane_index, plane_x_offset, plane_metapixels - scroll_offset, blit_lookup_list);
	}
}

static void RenderForegroundPlane(const VDP* const vdp, const cc_u8f left_boundary, const cc_u8f right_boundary, const cc_u16f scanline, cc_u8l* const plane_metapixels, const VDP_BlitLookupLower* const blit_lookup_list, const cc_bool window_plane)
{
	/* Notably, we allow Plane A to render in the Window Plane's place when the latter is disabled. */
	if (window_plane && !vdp->configuration->window_disabled)
	{
		/* Left-aligned window plane. */
		RenderWindowPlane(vdp, left_boundary, right_boundary, scanline, plane_metapixels, blit_lookup_list);
	}
	else
	{
		/* Scrolling plane. */
		RenderScrollPlane(vdp, left_boundary, right_boundary, scanline, plane_metapixels, blit_lookup_list, 0);
	}
}

static void RenderSpritePlane(const VDP* const vdp, cc_u8l* const plane_metapixels, cc_u8l (* const sprite_metapixels)[2], const VDP_BlitLookupLower* const blit_lookup_list, const unsigned int mask, const cc_u16f left_boundary_pixels, const cc_u16f right_boundary_pixels)
{
	const cc_u8l *sprite_metapixels_pointer = sprite_metapixels[left_boundary_pixels];
	cc_u8l *plane_metapixels_pointer = &plane_metapixels[left_boundary_pixels];

	cc_u16f i;

	for (i = left_boundary_pixels; i < right_boundary_pixels; ++i)
	{
		*plane_metapixels_pointer = blit_lookup_list[sprite_metapixels_pointer[1]][*plane_metapixels_pointer][sprite_metapixels_pointer[0]] & mask;
		++plane_metapixels_pointer;
		sprite_metapixels_pointer += 2;
	}
}

static void RenderForegroundAndSpritePlanes(const VDP* const vdp, const cc_u16f scanline, cc_u8l* const plane_metapixels, cc_u8l (* const sprite_metapixels)[2], const cc_bool window_plane, const VDP_ScanlineRenderedCallback scanline_rendered_callback, const void* const scanline_rendered_callback_user_data)
{
	const VDP_Constant* const constant = vdp->constant;
	VDP_State* const state = vdp->state;

	const cc_bool full_window_plane_line = (scanline < state->window.vertical_boundary) != state->window.aligned_bottom;

	const cc_u8f left_boundary = full_window_plane_line ? 0 : state->window.aligned_right == window_plane ? state->window.horizontal_boundary : 0;
	const cc_u8f right_boundary = full_window_plane_line ? window_plane ? SCANLINE_WIDTH_IN_TILE_PAIRS : 0 : state->window.aligned_right == window_plane ? SCANLINE_WIDTH_IN_TILE_PAIRS : state->window.horizontal_boundary;

	const cc_u16f left_boundary_pixels = left_boundary * TILE_PAIR_WIDTH;
	const cc_u16f right_boundary_pixels = right_boundary * TILE_PAIR_WIDTH;

	cc_u8f plane_index;

	if (left_boundary == right_boundary)
		return;

	if (state->display_enabled)
	{
		RenderForegroundPlane(vdp, left_boundary, right_boundary, scanline, plane_metapixels, constant->blit_lookup.normal, window_plane);

		if (state->shadow_highlight_enabled)
			RenderSpritePlane(vdp, plane_metapixels, sprite_metapixels, constant->blit_lookup.shadow_highlight, 0xFF, left_boundary_pixels, right_boundary_pixels);
		else
			RenderSpritePlane(vdp, plane_metapixels, sprite_metapixels, constant->blit_lookup.normal, 0x3F, left_boundary_pixels, right_boundary_pixels);

		switch (state->debug.forced_layer)
		{
			case 1:
				RenderSpritePlane(vdp, plane_metapixels, sprite_metapixels, constant->blit_lookup.forced_layer, 0xFF, left_boundary_pixels, right_boundary_pixels);
				break;

			case 2:
				RenderScrollPlane(vdp, left_boundary, right_boundary, scanline, plane_metapixels, constant->blit_lookup.forced_layer, 0);
				break;

			case 3:
				RenderScrollPlane(vdp, left_boundary, right_boundary, scanline, plane_metapixels, constant->blit_lookup.forced_layer, 1);
				break;
		}
	}

	/* Send pixels to the frontend to be displayed */
	scanline_rendered_callback((void*)scanline_rendered_callback_user_data, scanline, plane_metapixels, left_boundary_pixels, right_boundary_pixels, (state->h40_enabled ? 40 : 32) * TILE_WIDTH, MULTIPLY_BY_TILE_HEIGHT(state, state->v30_enabled ? 30 : 28));
}

void VDP_RenderScanline(const VDP* const vdp, const cc_u16f scanline, const VDP_ScanlineRenderedCallback scanline_rendered_callback, const void* const scanline_rendered_callback_user_data)
{
	const VDP_Constant* const constant = vdp->constant;
	VDP_State* const state = vdp->state;

	/* The padding bytes of the left and right are for allowing tile pairs to overdraw at the
	   edges of the screen. */
	cc_u8l plane_metapixels_buffer[(1 + SCANLINE_WIDTH_IN_TILE_PAIRS + 1) * TILE_PAIR_WIDTH];
	cc_u8l* const plane_metapixels = &plane_metapixels_buffer[TILE_PAIR_WIDTH];

	/* The padding bytes of the left and right are for allowing sprites to overdraw at the
	   edges of the screen. */
	cc_u8l sprite_metapixels_buffer[(MAX_SPRITE_WIDTH - 1) + VDP_MAX_SCANLINE_WIDTH + (MAX_SPRITE_WIDTH - 1)][2];
	cc_u8l (* const sprite_metapixels)[2] = &sprite_metapixels_buffer[MAX_SPRITE_WIDTH - 1];

	assert(scanline < VDP_MAX_SCANLINES);

	UpdateSpriteCache(state);

	/* Clear the scanline buffer, so that the sprite blitter
	   knows which pixels haven't been drawn yet. */
	memset(sprite_metapixels_buffer, 0, sizeof(sprite_metapixels_buffer));

	if (!vdp->configuration->sprites_disabled)
		RenderSprites(sprite_metapixels_buffer, state, scanline);

	/* Fill the scanline buffer with the background colour. */
	/* When forcing a layer, we set all the colour bits to simulate it replacing the background colour layer (since it is ANDed). */
	memset(plane_metapixels, state->debug.forced_layer == 0 ? state->background_colour : 0x3F, VDP_MAX_SCANLINE_WIDTH);

	if (state->display_enabled)
	{
		/* Draw Plane B. */
		RenderScrollPlane(vdp, 0, SCANLINE_WIDTH_IN_TILE_PAIRS, scanline, plane_metapixels, constant->blit_lookup.normal, 1);
	}

	/* Draw Window Plane (and sprites). */
	RenderForegroundAndSpritePlanes(vdp, scanline, plane_metapixels, sprite_metapixels, cc_true,  scanline_rendered_callback, scanline_rendered_callback_user_data);

	/* Draw Plane A (and sprites). */
	RenderForegroundAndSpritePlanes(vdp, scanline, plane_metapixels, sprite_metapixels, cc_false, scanline_rendered_callback, scanline_rendered_callback_user_data);
}

cc_u16f VDP_ReadData(const VDP* const vdp)
{
	cc_u16f value = 0;

	vdp->state->access.write_pending = cc_false;

	if (!IsInReadMode(vdp->state))
	{
		/* According to GENESIS SOFTWARE DEVELOPMENT MANUAL (COMPLEMENT) section 4.1,
		   this should cause the 68k to hang */
		/* TODO */
		LogMessage("Data was read from the VDP data port while the VDP was in write mode");
	}
	else
	{
		value = ReadAndIncrement(vdp->state);
	}

	return value;
}

cc_u16f VDP_ReadControl(const VDP* const vdp)
{
	const cc_bool fifo_empty = cc_true;

	/* Reading from the control port aborts the VDP waiting for a second command word to be written.
	   This doesn't appear to be documented in the official SDK manuals we have, but the official
	   boot code makes use of this feature. */
	vdp->state->access.write_pending = cc_false;

	/* TODO: The other flags. */
	return 0x3400 | (fifo_empty << 9) | (vdp->state->currently_in_vblank << 3);
}

static void UpdateFakeFIFO(VDP_State* const state, const cc_u16f value)
{
	const cc_u8f last = CC_COUNT_OF(state->previous_data_writes) - 1;
	cc_u8f i;

	for (i = 0; i < last; ++i)
		state->previous_data_writes[i] = state->previous_data_writes[i + 1];

	state->previous_data_writes[last] = value;
}

void VDP_WriteData(const VDP* const vdp, const cc_u16f value, const VDP_ColourUpdatedCallback colour_updated_callback, const void* const colour_updated_callback_user_data)
{
	vdp->state->access.write_pending = cc_false;

	UpdateFakeFIFO(vdp->state, value);

	if (IsInReadMode(vdp->state))
	{
		/* Invalid input, but defined behaviour */
		LogMessage("Data was written to the VDP data port while the VDP was in read mode");

		/* According to GENESIS SOFTWARE DEVELOPMENT MANUAL (COMPLEMENT) section 4.1,
		   data should not be written, but the address should be incremented */
		IncrementAddressRegister(vdp->state);
	}
	else
	{
		/* Write the value to memory */
		WriteAndIncrement(vdp->state, value, colour_updated_callback, colour_updated_callback_user_data);

		if (IsDMAPending(vdp->state))
		{
			/* Perform DMA fill */
			/* TODO: https://gendev.spritesmind.net/forum/viewtopic.php?p=31857&sid=34ef0ab3215fa6ceb29e12db824c3427#p31857 */
			ClearDMAPending(vdp->state);

			do
			{
				if (vdp->state->access.selected_buffer == VDP_ACCESS_VRAM)
				{
					WriteVRAM(vdp->state, vdp->state->access.address_register, (cc_u8f)(value >> 8));
					IncrementAddressRegister(vdp->state);
				}
				else
				{
					/* On real Mega Drives, the fill value for CRAM and VSRAM is fetched from earlier in the FIFO, which appears to be a bug. */
					/* Verified with Nemesis' 'VDPFIFOTesting' homebrew. */
					WriteAndIncrement(vdp->state, vdp->state->previous_data_writes[0], colour_updated_callback, colour_updated_callback_user_data);
				}

				/* Yes, even DMA fills do this, according to
				   'https://gendev.spritesmind.net/forum/viewtopic.php?p=21016#p21016'. */
				++vdp->state->dma.source_address_low;
				vdp->state->dma.source_address_low &= 0xFFFF;
			} while (--vdp->state->dma.length, vdp->state->dma.length &= 0xFFFF, vdp->state->dma.length != 0);
		}
	}
}

/* TODO: Retention of partial commands. */
void VDP_WriteControl(const VDP* const vdp, const cc_u16f value, const VDP_ColourUpdatedCallback colour_updated_callback, const void* const colour_updated_callback_user_data, const VDP_ReadCallback read_callback, const void* const read_callback_user_data, const VDP_KDebugCallback kdebug_callback, const void* const kdebug_callback_user_data)
{
	if (vdp->state->access.write_pending || (value & 0xC000) != 0x8000)
	{
		if (vdp->state->access.write_pending)
		{
			/* This is an "address set" command (part 2). */
			const cc_u16f code_bitmask = vdp->state->dma.enabled ? 0x3C : 0x1C;

			vdp->state->access.write_pending = cc_false;
			vdp->state->access.address_register = (vdp->state->access.address_register & 0x3FFF) | ((value & 7) << 14);
			vdp->state->access.code_register = (vdp->state->access.code_register & ~code_bitmask) | ((value >> 2) & code_bitmask);
		}
		else
		{
			/* This is an "address set" command (part 1). */
			vdp->state->access.write_pending = cc_true;
			vdp->state->access.address_register = (value & 0x3FFF) | (vdp->state->access.address_register & (3 << 14));
			vdp->state->access.code_register = ((value >> 14) & 3) | (vdp->state->access.code_register & 0x3C);
		}

		switch ((vdp->state->access.code_register >> 1) & 7)
		{
		case 0: /* VRAM */
			vdp->state->access.selected_buffer = VDP_ACCESS_VRAM;
			break;

		case 4: /* CRAM (read) */
		case 1: /* CRAM (write) */
			vdp->state->access.selected_buffer = VDP_ACCESS_CRAM;
			break;

		case 2: /* VSRAM */
			vdp->state->access.selected_buffer = VDP_ACCESS_VSRAM;
			break;

		case 6: /* VRAM (8-bit, undocumented) */
			vdp->state->access.selected_buffer = VDP_ACCESS_VRAM_8BIT;
			break;

		default: /* Invalid */
			vdp->state->access.selected_buffer = VDP_ACCESS_INVALID;
			break;
		}
	}
	else
	{
		/* This is a "register set" command. */
		const cc_u8f reg = (value >> 8) & 0x1F;
		const cc_u8f data = value & 0xFF;

		/* This is relied upon by Sonic 3D Blast (the opening FMV will have broken colours otherwise). */
		/* This is further verified by Nemesis' 'VDPFIFOTesting' homebrew. */
		vdp->state->access.selected_buffer = VDP_ACCESS_INVALID;

		/* This command is setting a register */
		if (reg <= 10 || vdp->state->mega_drive_mode_enabled)
		{
			switch (reg)
			{
				case 0:
					/* MODE SET REGISTER NO.1 */

					/* TODO */
					if ((data & (1 << 5)) != 0)
							LogMessage("'Blank 8 leftmost pixel columns' flag set but is currently unemulated.");

					vdp->state->h_int_enabled = (data & (1 << 4)) != 0;

					/* TODO */
					if ((data & (1 << 1)) != 0)
							LogMessage("'Latch H/V counter' flag set but is currently unemulated.");

					break;

				case 1:
					/* MODE SET REGISTER NO.2 */
					vdp->state->extended_vram_enabled = (data & (1 << 7)) != 0;
					vdp->state->display_enabled = (data & (1 << 6)) != 0;
					vdp->state->v_int_enabled = (data & (1 << 5)) != 0;
					vdp->state->dma.enabled = (data & (1 << 4)) != 0;
					vdp->state->v30_enabled = (data & (1 << 3)) != 0;
					vdp->state->mega_drive_mode_enabled = (data & (1 << 2)) != 0;
					break;

				case 2:
					/* PATTERN NAME TABLE BASE ADDRESS FOR SCROLL A */
					vdp->state->plane_a_address = (data & 0x78) << 10;
					break;

				case 3:
					/* PATTERN NAME TABLE BASE ADDRESS FOR WINDOW */
					/* TODO: The lowest bit is invalid is H40 mode according to the 'Genesis Software Manual'. */
					/* http://techdocs.exodusemulator.com/Console/SegaMegaDrive/Documentation.html#mega-drive-documentation */
					vdp->state->window_address = (data & 0x7E) << 10;
					break;

				case 4:
					/* PATTERN NAME TABLE BASE ADDRESS FOR SCROLL B */
					vdp->state->plane_b_address = (data & 0xF) << 13;
					break;

				case 5:
					/* SPRITE ATTRIBUTE TABLE BASE ADDRESS */
					vdp->state->sprite_table_address = (cc_u32f)data << 9;

					/* Real VDPs partially cache the sprite table, and forget to update it
					   when the sprite table base address is changed. Replicating this
					   behaviour may be needed in order to properly emulate certain effects
					   that involve manipulating the sprite table during rendering. */
					/*vdp->state->sprite_row_cache.needs_updating = cc_true;*/ /* The VDP does not do this! */

					break;

				case 6:
					/* Unused legacy register for Master System mode. */
					vdp->state->sprite_tile_index_rebase = (data & (1 << 5)) != 0;

					break;

				case 7:
					/* BACKGROUND COLOR */
					vdp->state->background_colour = data & 0x3F;
					break;

				case 8:
				case 9:
					/* Unused legacy register for Master System mode. */
					/* TODO */
					break;

				case 10:
					/* H INTERRUPT REGISTER */
					vdp->state->h_int_interval = (cc_u8l)data;
					break;

				case 11:
					/* MODE SET REGISTER NO.3 */

					/* TODO */
					if ((data & (1 << 3)) != 0)
							LogMessage("'External interrupt' flag set but is currently unemulated.");

					vdp->state->vscroll_mode = data & 4 ? VDP_VSCROLL_MODE_2CELL : VDP_VSCROLL_MODE_FULL;
					SetHScrollMode(vdp->state, (VDP_HScrollMode)(data & 3));
					break;

				case 12:
					/* MODE SET REGISTER NO.4 */
					/* TODO: Mode 1. */
					/* TODO: This register is latched on V-Int: https://gendev.spritesmind.net/forum/viewtopic.php?t=768 */
					vdp->state->h40_enabled = (data & ((1 << 7) | (1 << 0))) != 0;
					vdp->state->shadow_highlight_enabled = (data & (1 << 3)) != 0;

					/* Process the interlacing bits.
					   To fully understand this, one has to understand how PAL and NTSC televisions display:
					   NTSC televisions display 480 lines, however, they are divided into two 'fields' of
					   240 lines. On one frame the cathode ray draws the even-numbered lines, then on the
					   next frame it draw the odd-numbered lines. */
					switch ((data >> 1) & 3)
					{
						case 0:
							/* Regular '240p' mode: the 'Genesis Software Manual' seems to suggest that this
							   mode only outputs the even-numbered lines, leaving the odd-numbered ones black. */
							vdp->state->double_resolution_enabled = cc_false;
							break;

						case 1:
							/* This mode renders the odd-numbered lines as well, but they are merely
							   duplicates of the even lines. The 'Genesis Software Manual' warns that this
							   can create severe vertical blurring. */
							vdp->state->double_resolution_enabled = cc_false;
							break;

						case 2:
							/* The 'Genesis Software Manual' warns that this is prohibited. Some unlicensed
							   EA games use this. Apparently it creates an image that is slightly broken in
							   some way. */
							vdp->state->double_resolution_enabled = cc_false;
							LogMessage("Invalid interlace setting '2' used.");
							break;

						case 3:
							/* Double resolution mode: in this mode, the odd and even lines display different
							   graphics, effectively turning the tiles from 8x8 to 8x16. */
							vdp->state->double_resolution_enabled = cc_true;
							break;
					}

					break;

				case 13:
					/* H SCROLL DATA TABLE BASE ADDRESS */
					vdp->state->hscroll_address = (data & 0x7F) << 10;
					break;

				case 14:
					/* PATTERN NAME TABLE BASE ADDRESS 128KiB */
					vdp->state->plane_a_tile_index_rebase = (data & (1 << 0)) != 0;
					vdp->state->plane_b_tile_index_rebase = (data & (1 << 4)) != 0 && vdp->state->plane_a_tile_index_rebase;
					break;

				case 15:
					/* AUTO INCREMENT DATA */
					vdp->state->access.increment = (cc_u8l)data;
					break;

				case 16:
					/* SCROLL SIZE */
					/* https://gendev.spritesmind.net/forum/viewtopic.php?p=31307#p31307 */
					vdp->state->plane_height_bitmask = (data << 1) | 0x1F;

					switch (data & 3)
					{
						case 0:
							vdp->state->plane_width_shift = 5;
							vdp->state->plane_height_bitmask &= 0x7F;
							break;

						case 1:
							vdp->state->plane_width_shift = 6;
							vdp->state->plane_height_bitmask &= 0x3F;
							break;

						case 2:
							vdp->state->plane_width_shift = 5;
							vdp->state->plane_height_bitmask &= 0;
							break;

						case 3:
							vdp->state->plane_width_shift = 7;
							vdp->state->plane_height_bitmask &= 0x1F;
							break;
					}

					break;

				case 17:
					/* WINDOW H POSITION */
					vdp->state->window.aligned_right = (data & 0x80) != 0;
					vdp->state->window.horizontal_boundary = CC_MIN(SCANLINE_WIDTH_IN_TILE_PAIRS, data & 0x1F); /* Measured in tile pairs. */
					break;

				case 18:
					/* WINDOW V POSITION */
					vdp->state->window.aligned_bottom = (data & 0x80) != 0;
					vdp->state->window.vertical_boundary = MULTIPLY_BY_TILE_HEIGHT(vdp->state, data & 0x1F); /* Measured in tiles. */
					break;

				case 19:
					/* DMA LENGTH COUNTER LOW */
					vdp->state->dma.length &= ~(0xFFu << 0);
					vdp->state->dma.length |= data << 0;
					break;

				case 20:
					/* DMA LENGTH COUNTER HIGH */
					vdp->state->dma.length &= ~(0xFFu << 8);
					vdp->state->dma.length |= data << 8;
					break;

				case 21:
					/* DMA SOURCE ADDRESS LOW */
					vdp->state->dma.source_address_low &= ~(0xFFu << 0);
					vdp->state->dma.source_address_low |= data << 0;
					break;

				case 22:
					/* DMA SOURCE ADDRESS MID. */
					vdp->state->dma.source_address_low &= ~(0xFFu << 8);
					vdp->state->dma.source_address_low |= data << 8;
					break;

				case 23:
					/* DMA SOURCE ADDRESS HIGH */
					if ((data & 0x80) != 0)
					{
						vdp->state->dma.source_address_high = data & 0x3F;
						vdp->state->dma.mode = (data & 0x40) != 0 ? VDP_DMA_MODE_COPY : VDP_DMA_MODE_FILL;
					}
					else
					{
						vdp->state->dma.source_address_high = data & 0x7F;
						vdp->state->dma.mode = VDP_DMA_MODE_MEMORY_TO_VRAM;
					}

					break;

				case 30:
				{
					/* Gens KMod debug register. Does not exist in real Mega Drives, but is a useful emulator feature for debugging. */
					const char character = CC_SIGN_EXTEND(int, 7, (int)data);

					/* This behaviour exactly matches Gens KMod v0.7. */
					if (character < 0x20 && character != '\0')
						break;

					vdp->state->kdebug_buffer[vdp->state->kdebug_buffer_index++] = character;

					/* The last byte of the buffer is always set to 0, so we don't need to do it here. */
					if (character == '\0' || vdp->state->kdebug_buffer_index == CC_COUNT_OF(vdp->state->kdebug_buffer) - 1)
					{
						vdp->state->kdebug_buffer_index = 0;
						kdebug_callback((void*)kdebug_callback_user_data, vdp->state->kdebug_buffer);
					}

					break;
				}

				default:
					/* Invalid */
					LogMessage("Attempted to set invalid VDP register (0x%" CC_PRIXFAST16 ")", reg);
					break;
			}
		}
	}

	if (IsDMAPending(vdp->state) && vdp->state->dma.mode != VDP_DMA_MODE_FILL)
	{
		/* Firing DMA */
		ClearDMAPending(vdp->state);

		do
		{
			if (vdp->state->dma.mode == VDP_DMA_MODE_MEMORY_TO_VRAM)
			{
				const cc_u16f value = read_callback((void*)read_callback_user_data, ((cc_u32f)vdp->state->dma.source_address_high << 17) | ((cc_u32f)vdp->state->dma.source_address_low << 1));
				UpdateFakeFIFO(vdp->state, value);
				WriteAndIncrement(vdp->state, value, colour_updated_callback, colour_updated_callback_user_data);
			}
			else /*if (state->dma.mode == VDP_DMA_MODE_COPY)*/
			{
				WriteVRAM(vdp->state, vdp->state->access.address_register, ReadVRAM(vdp->state, vdp->state->dma.source_address_low));
				IncrementAddressRegister(vdp->state);
			}

			/* Emulate the 128KiB DMA wrap-around bug. */
			++vdp->state->dma.source_address_low;
			vdp->state->dma.source_address_low &= 0xFFFF;
		} while (--vdp->state->dma.length, vdp->state->dma.length &= 0xFFFF, vdp->state->dma.length != 0);
	}
}

void VDP_WriteDebug(const VDP* const vdp, const cc_u16f value)
{
	vdp->state->debug.forced_layer = (value >> 7) & 3;
}

/* TODO: Delete this? */
cc_u16f VDP_ReadVRAMWord(const VDP_State* const state, const cc_u16f address)
{
	return READ_VRAM_WORD(state, address);
}

VDP_TileMetadata VDP_DecomposeTileMetadata(const cc_u16f packed_tile_metadata)
{
	VDP_TileMetadata tile_metadata;

	tile_metadata.tile_index = VDP_GetTileIndex(packed_tile_metadata);
	tile_metadata.palette_line = VDP_GetTilePaletteLine(packed_tile_metadata);
	tile_metadata.x_flip = VDP_GetTileXFlip(packed_tile_metadata);
	tile_metadata.y_flip = VDP_GetTileYFlip(packed_tile_metadata);
	tile_metadata.priority = VDP_GetTilePriority(packed_tile_metadata);

	return tile_metadata;
}

VDP_CachedSprite VDP_GetCachedSprite(const VDP_State* const state, const cc_u16f sprite_index)
{
	VDP_CachedSprite cached_sprite;

	const cc_u8l* const bytes = state->sprite_table_cache[sprite_index];

	cached_sprite.y = (bytes[0] | ((bytes[1] & 3) << 8)) & (0x3FF >> !state->double_resolution_enabled);
	cached_sprite.link = bytes[2] & 0x7F;
	cached_sprite.width = ((bytes[3] >> 2) & 3) + 1;
	cached_sprite.height = (bytes[3] & 3) + 1;

	return cached_sprite;
}
