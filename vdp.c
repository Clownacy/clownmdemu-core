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

enum
{
	SHADOW_HIGHLIGHT_NORMAL = 0 << 6,
	SHADOW_HIGHLIGHT_SHADOW = 1 << 6,
	SHADOW_HIGHLIGHT_HIGHLIGHT = 2 << 6
};

typedef struct TileInfo
{
	cc_u16f height_power, height_mask, size;
} TileInfo;

static unsigned int GetTileHeightPower(const VDP_State* const state)
{
	return state->double_resolution_enabled ? 4 : 3;
}

static TileInfo MakeTileInfo(const VDP_State* const state)
{
	TileInfo tile_info;
	tile_info.height_power = GetTileHeightPower(state);
	tile_info.height_mask = (1 << tile_info.height_power) - 1;
	tile_info.size = (TILE_WIDTH << tile_info.height_power) / 2;
	return tile_info;
}

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

static void WriteVRAM(VDP_State* const state, const cc_u16f index, const cc_u8f value)
{
	/* Update sprite cache if we're writing to the sprite table */
	/* TODO: Do DMA fills and copies do this? */
	const cc_u16f index_wrapped = index % CC_COUNT_OF(state->vram);
	const cc_u16f sprite_table_index = index_wrapped - state->sprite_table_address;

	if (sprite_table_index < (state->h40_enabled ? 80u : 64u) * 8u && (sprite_table_index & 4) == 0)
	{
		cc_u8l* const cache_bytes = state->sprite_table_cache[sprite_table_index / 8];

		cache_bytes[sprite_table_index & 3] = value;

		state->sprite_row_cache.needs_updating = cc_true;
	}

	state->vram[index_wrapped] = value;
}

static void WriteAndIncrement(VDP_State* const state, const cc_u16f value, const VDP_ColourUpdatedCallback colour_updated_callback, const void* const colour_updated_callback_user_data)
{
	switch (state->access.selected_buffer)
	{
		case VDP_ACCESS_VRAM:
			WriteVRAM(state, state->access.address_register ^ 0, (cc_u8f)(value >> 8));
			WriteVRAM(state, state->access.address_register ^ 1, (cc_u8f)(value & 0xFF));
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

	state->access.address_register += state->access.increment;
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
			value = VDP_ReadVRAMWord(state, (word_address * 2) % CC_COUNT_OF(state->vram));
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
			value |= state->vram[(state->access.address_register ^ 1) % CC_COUNT_OF(state->vram)];
			break;

		default:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case VDP_ACCESS_INVALID:
			LogMessage("VDP read attempted with invalid access mode specified (0x%" CC_PRIXFAST16 ")", state->access.code_register);
			break;
	}

	state->access.address_register += state->access.increment;

	return value;
}

void VDP_Constant_Initialise(VDP_Constant* const constant)
{
	/* This essentially pre-computes the VDP's depth-test and alpha-test,
	   generating a lookup table to eliminate the need to perform these
	   every time a pixel is blitted. This provides a *massive* speed boost. */
	cc_u16f new_pixel_high;

	for (new_pixel_high = 0; new_pixel_high < CC_COUNT_OF(constant->blit_lookup); ++new_pixel_high)
	{
		cc_u16f old_pixel;

		for (old_pixel = 0; old_pixel < CC_COUNT_OF(constant->blit_lookup[0]); ++old_pixel)
		{
			cc_u16f new_pixel_low;

			for (new_pixel_low = 0; new_pixel_low < CC_COUNT_OF(constant->blit_lookup[0][0]); ++new_pixel_low)
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

				constant->blit_lookup[new_pixel_high][old_pixel][new_pixel_low] = (cc_u8l)output;

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

				constant->blit_lookup_shadow_highlight[new_pixel_high][old_pixel][new_pixel_low] = (cc_u8l)output;
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

	state->plane_width_bitmask = 0x1F;
	state->plane_height_bitmask = 0x1F;

	state->display_enabled = cc_false;
	state->v_int_enabled = cc_false;
	state->h_int_enabled = cc_false;
	state->h40_enabled = cc_false;
	state->v30_enabled = cc_false;
	state->mega_drive_mode_enabled = cc_false;
	state->shadow_highlight_enabled = cc_false;
	state->double_resolution_enabled = cc_false;

	state->background_colour = 0;
	state->h_int_interval = 0;
	state->currently_in_vblank = cc_false;
	state->allow_sprite_masking = cc_false;

	state->hscroll_mode = VDP_HSCROLL_MODE_FULL;
	state->vscroll_mode = VDP_VSCROLL_MODE_FULL;

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

static cc_u16f GetHScrollTableOffset(const VDP_State* const state, const cc_u16f scanline, const TileInfo* const tile_info)
{
	switch (state->hscroll_mode)
	{
		default:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case VDP_HSCROLL_MODE_FULL:
			return 0;

		case VDP_HSCROLL_MODE_INVALID:
			return ((scanline >> state->double_resolution_enabled) % 8) * 4;

		case VDP_HSCROLL_MODE_1CELL:
			return (scanline >> tile_info->height_power << tile_info->height_power) * 4;

		case VDP_HSCROLL_MODE_1LINE:
			return (scanline >> state->double_resolution_enabled) * 4;
	}
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

static void RenderTile(const VDP* const vdp, const cc_u8f start, const cc_u8f end, const cc_u16f pixel_y_in_plane, const cc_u16f vram_address, const TileInfo* const tile_info, cc_u8l** const metapixels_pointer)
{
	const VDP_TileMetadata tile = VDP_DecomposeTileMetadata(VDP_ReadVRAMWord(vdp->state, vram_address));

	/* Get the Y coordinate of the pixel in the tile */
	const cc_u16f pixel_y_in_tile = (pixel_y_in_plane & tile_info->height_mask) ^ (tile.y_flip ? tile_info->height_mask : 0);

	/* Get raw tile data that contains the desired metapixel */
	const cc_u8l* const tile_data = &vdp->state->vram[(tile.tile_index * tile_info->size + pixel_y_in_tile * 4) % CC_COUNT_OF(vdp->state->vram)];

	const cc_u8f byte_index_xor = tile.x_flip ? 7 : 0;
	const cc_u8f metapixel_upper_bits = (tile.priority << 2) | tile.palette_line;

	const cc_u8l (* const blit_lookup)[1 << 4] = vdp->constant->blit_lookup[metapixel_upper_bits];

	cc_u8f i;

	for (i = start; i < end && i < TILE_WIDTH; ++i)
	{
		/* Get the X coordinate of the pixel in the tile */
		const cc_u8f pixel_x_in_tile = i ^ byte_index_xor;

		/* Obtain the index into the palette line */
		const cc_u8f nibble_shift = (~pixel_x_in_tile & 1) << 2;
		const cc_u8f palette_line_index = (tile_data[pixel_x_in_tile / 2] >> nibble_shift) & 0xF;

		**metapixels_pointer = blit_lookup[**metapixels_pointer][palette_line_index];
		++*metapixels_pointer;
	}
}

static void RenderTilePair(const VDP* const vdp, const cc_u16f start, const cc_u16f end, const cc_u16f pixel_y_in_plane, const cc_u16f vram_address, const TileInfo* const tile_info, cc_u8l** const metapixels_pointer)
{
	cc_u8f i;

	for (i = 0; i < TILE_PAIR_COUNT; ++i)
	{
		const cc_u8f lower = TILE_WIDTH * i;
		const cc_u8f upper = lower + TILE_WIDTH;

		RenderTile(vdp, CC_CLAMP(lower, upper, start) - lower, CC_CLAMP(lower, upper, end) - lower, pixel_y_in_plane, vram_address + i * 2, tile_info, metapixels_pointer);
	}
}

static void RenderScrollingPlane(const VDP* const vdp, const cc_u8f start, const cc_u8f end, const TileInfo* const tile_info, const cc_u16f scanline, const cc_u8f plane_index, const cc_u16f plane_x_offset, const cc_u16f scroll_offset, cc_u8l* const metapixels)
{
	VDP_State* const state = vdp->state;

	const cc_u16f plane_width_bitmask = state->plane_width_bitmask;
	const cc_u16f plane_height_bitmask = state->plane_height_bitmask;
	const cc_u16f plane_pitch = plane_width_bitmask + 1;
	const cc_u16f plane_address = plane_index == 0 ? state->plane_a_address : state->plane_b_address;

	const cc_u16f tile_height_power = tile_info->height_power;

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
		const cc_u16f tile_y = (pixel_y_in_plane >> tile_height_power) & plane_height_bitmask;
		const cc_u16f vram_address = plane_address + (tile_y * plane_pitch + tile_x) * 2;

		RenderTilePair(vdp, i == start ? scroll_offset : 0, i == end ? scroll_offset : TILE_PAIR_WIDTH, pixel_y_in_plane, vram_address, tile_info, &metapixels_pointer);
	}
}

static void RenderWindowPlane(const VDP* const vdp, const cc_u8f start, const cc_u8f end, const cc_u16f scanline, const TileInfo* const tile_info, cc_u8l* const metapixels)
{
	const VDP_State* const state = vdp->state;

	const cc_u16f tile_y = scanline >> tile_info->height_power;
	const cc_u16f window_plane_pitch = 32 << state->h40_enabled;

	cc_u8l *metapixels_pointer = &metapixels[start * TILE_PAIR_WIDTH];
	cc_u16f vram_address = state->window_address + (tile_y * window_plane_pitch + start * TILE_PAIR_COUNT) * 2;

	cc_u8f i;

	/* Render tiles */
	for (i = start; i < end && i < SCANLINE_WIDTH_IN_TILE_PAIRS; ++i)
	{
		RenderTilePair(vdp, 0, TILE_PAIR_WIDTH, scanline, vram_address, tile_info, &metapixels_pointer);
		vram_address += 4;
	}
}

static void RenderSprites(cc_u8l (* const sprite_metapixels)[2], VDP_State* const state, const cc_u16f scanline, const TileInfo* const tile_info)
{
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
		const cc_u16f sprite_index = state->sprite_table_address + sprite_row_cache_entry->table_index * 8;
		const cc_u16f width = sprite_row_cache_entry->width;
		const cc_u16f height = sprite_row_cache_entry->height;
		const VDP_TileMetadata tile = VDP_DecomposeTileMetadata(VDP_ReadVRAMWord(state, sprite_index + 4));
		const cc_u16f x = VDP_ReadVRAMWord(state, sprite_index + 6) & 0x1FF;

		const cc_u8f metapixel_high_bits = (tile.priority << 2) | tile.palette_line;

		cc_u16f y_in_sprite = sprite_row_cache_entry->y_in_sprite;

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
			cc_u8l *metapixels_pointer = sprite_metapixels[(MAX_SPRITE_WIDTH - 1) + x - 0x80];

			cc_u16f j;

			y_in_sprite = tile.y_flip ? (height << tile_info->height_power) - y_in_sprite - 1 : y_in_sprite;

			for (j = 0; j < width; ++j)
			{
				const cc_u16f x_in_sprite = tile.x_flip ? width - j - 1 : j;
				const cc_u16f tile_index = tile.tile_index + (y_in_sprite >> tile_info->height_power) + x_in_sprite * height;
				const cc_u16f pixel_y_in_tile = y_in_sprite & tile_info->height_mask;

				/* Get raw tile data that contains the desired metapixel */
				const cc_u8l* const tile_data = &state->vram[(tile_index * tile_info->size + pixel_y_in_tile * 4) % CC_COUNT_OF(state->vram)];

				cc_u16f k;

				for (k = 0; k < TILE_WIDTH; ++k)
				{
					/* Get the X coordinate of the pixel in the tile */
					const cc_u8f pixel_x_in_tile = k ^ (tile.x_flip ? 7 : 0);

					/* Obtain the index into the palette line */
					const cc_u8f nibble_shift = (~pixel_x_in_tile & 1) << 2;
					const cc_u8f palette_line_index = (tile_data[pixel_x_in_tile / 2] >> nibble_shift) & 0xF;

					const cc_u8f mask = 0 - (cc_u8f)((*metapixels_pointer == 0) & (palette_line_index != 0));

					*metapixels_pointer++ |= palette_line_index & mask;
					*metapixels_pointer++ |= metapixel_high_bits & mask;

					if (--pixel_limit == 0)
						return;
				}
			}
		}

		if (--sprite_limit == 0)
			break;
	}

	/* Prevent sprite masking when ending a scanline without reaching the pixel limit. */
	state->allow_sprite_masking = cc_false;
}

void VDP_RenderScanline(const VDP* const vdp, const cc_u16f scanline, const VDP_ScanlineRenderedCallback scanline_rendered_callback, const void* const scanline_rendered_callback_user_data)
{
	const VDP_Constant* const constant = vdp->constant;
	VDP_State* const state = vdp->state;
	const TileInfo tile_info = MakeTileInfo(state);

	cc_u16f i;

	/* TODO: Should this just be the normal scanline width? */
	cc_u8l plane_metapixels[SCANLINE_WIDTH_IN_TILE_PAIRS * TILE_PAIR_WIDTH];

	assert(scanline < VDP_MAX_SCANLINES);

	/* Fill the scanline buffer with the background colour */
	for (i = 0; i < CC_COUNT_OF(plane_metapixels); ++i)
		plane_metapixels[i] = state->background_colour;

	if (state->display_enabled)
	{
		/* ***** *
		 * Setup *
		 * ***** */

		/* The padding bytes of the left and right are for allowing sprites to overdraw at the
		   edges of the screen. */
		cc_u8l sprite_metapixels[(MAX_SPRITE_WIDTH - 1) + VDP_MAX_SCANLINE_WIDTH + (MAX_SPRITE_WIDTH - 1)][2];

		cc_u8f plane_index;

		/* *********** */
		/* Draw planes */
		/* *********** */
		for (plane_index = 2; plane_index-- > 0; )
		{
			/* Notably, we allow Plane A to render in the Window Plane's place when the latter is disabled. */
			/* TODO: Test if this works properly in Interlace Mode 2. */
			const cc_bool window_plane_active = plane_index == 0 && !vdp->configuration->window_disabled;
			const cc_bool full_window_plane_line = window_plane_active && (scanline < state->window.vertical_boundary) != state->window.aligned_bottom;

			const cc_u8f left_boundary = full_window_plane_line ? 0 : window_plane_active && !state->window.aligned_right ? state->window.horizontal_boundary : 0;
			const cc_u8f right_boundary = full_window_plane_line ? 0 : window_plane_active && state->window.aligned_right ? state->window.horizontal_boundary : SCANLINE_WIDTH_IN_TILE_PAIRS;

			/* Left-aligned window plane. */
			RenderWindowPlane(vdp, 0, left_boundary, scanline, &tile_info, plane_metapixels);

			/* Scrolling plane. */
			if (!vdp->configuration->planes_disabled[plane_index])
			{
				const cc_u16f hscroll = VDP_ReadVRAMWord(state, state->hscroll_address + plane_index * 2 + GetHScrollTableOffset(state, scanline, &tile_info));

				/* Get the value used to offset the writes to the metapixel buffer */
				const cc_u16f scroll_offset = TILE_PAIR_WIDTH - (hscroll % TILE_PAIR_WIDTH);

				/* Get the value used to offset the reads from the plane map */
				const cc_u16f plane_x_offset = -(hscroll / TILE_PAIR_WIDTH);

				RenderScrollingPlane(vdp, left_boundary, right_boundary, &tile_info, scanline, plane_index, plane_x_offset, scroll_offset, plane_metapixels);
			}

			/* Right-aligned window plane. */
			RenderWindowPlane(vdp, right_boundary, SCANLINE_WIDTH_IN_TILE_PAIRS, scanline, &tile_info, plane_metapixels);
		}

		/* ************ *
		 * Draw sprites *
		 * ************ */

		/* Update the sprite row cache if needed */
		if (state->sprite_row_cache.needs_updating)
		{
			/* Caching and preprocessing some of the sprite table allows the renderer to avoid
			   scanning the entire sprite table every time it renders a scanline. The VDP actually
			   partially caches its sprite data too, though I don't know if it's for the same purpose. */
			const cc_u16f max_sprites = state->h40_enabled ? 80 : 64;

			cc_u16f sprite_index;
			cc_u16f sprites_remaining = max_sprites;

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
				for (i = CC_MAX(blank_lines, cached_sprite.y); i < CC_MIN(blank_lines + ((state->v30_enabled ? 30 : 28) << tile_info.height_power), cached_sprite.y + (cached_sprite.height << tile_info.height_power)); ++i)
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

		/* Clear the scanline buffer, so that the sprite blitter
			knows which pixels haven't been drawn yet. */
		memset(sprite_metapixels, 0, sizeof(sprite_metapixels));

		if (!vdp->configuration->sprites_disabled)
			RenderSprites(sprite_metapixels, state, scanline, &tile_info);

		/* ************************************ *
		 * Blit sprite pixels onto plane pixels *
		 * ************************************ */

		{
			const cc_u8l *sprite_metapixels_pointer = sprite_metapixels[MAX_SPRITE_WIDTH - 1];
			cc_u8l *plane_metapixels_pointer = plane_metapixels;

			if (state->shadow_highlight_enabled)
			{
				for (i = 0; i < VDP_MAX_SCANLINE_WIDTH; ++i)
				{
					*plane_metapixels_pointer = constant->blit_lookup_shadow_highlight[sprite_metapixels_pointer[1]][*plane_metapixels_pointer][sprite_metapixels_pointer[0]];
					++plane_metapixels_pointer;
					sprite_metapixels_pointer += 2;
				}
			}
			else
			{
				for (i = 0; i < VDP_MAX_SCANLINE_WIDTH; ++i)
				{
					*plane_metapixels_pointer = constant->blit_lookup[sprite_metapixels_pointer[1]][*plane_metapixels_pointer][sprite_metapixels_pointer[0]] & 0x3F;
					++plane_metapixels_pointer;
					sprite_metapixels_pointer += 2;
				}
			}
		}
	}

	/* Send pixels to the frontend to be displayed */
	scanline_rendered_callback((void*)scanline_rendered_callback_user_data, scanline, plane_metapixels, (state->h40_enabled ? 40 : 32) * TILE_WIDTH, (state->v30_enabled ? 30 : 28) << tile_info.height_power);
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
		vdp->state->access.address_register += vdp->state->access.increment;
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
					WriteVRAM(vdp->state, vdp->state->access.address_register ^ 1, (cc_u8f)(value >> 8));
					vdp->state->access.address_register += vdp->state->access.increment;
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
			vdp->state->access.address_register = (vdp->state->access.address_register & 0x3FFF) | ((value & 3) << 14);
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
		const cc_u16f reg = (value >> 8) & 0x1F;
		const cc_u16f data = value & 0xFF;

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
					vdp->state->h_int_enabled = (data & (1 << 4)) != 0;
					/* TODO */
					break;

				case 1:
					/* MODE SET REGISTER NO.2 */
					vdp->state->display_enabled = (data & (1 << 6)) != 0;
					vdp->state->v_int_enabled = (data & (1 << 5)) != 0;
					vdp->state->dma.enabled = (data & (1 << 4)) != 0;
					vdp->state->v30_enabled = (data & (1 << 3)) != 0;
					vdp->state->mega_drive_mode_enabled = (data & (1 << 2)) != 0;
					break;

				case 2:
					/* PATTERN NAME TABLE BASE ADDRESS FOR SCROLL A */
					vdp->state->plane_a_address = (data & 0x38) << 10;
					break;

				case 3:
					/* PATTERN NAME TABLE BASE ADDRESS FOR WINDOW */
					/* TODO: The lowest bit is invalid is H40 mode according to the 'Genesis Software Manual'. */
					/* http://techdocs.exodusemulator.com/Console/SegaMegaDrive/Documentation.html#mega-drive-documentation */
					vdp->state->window_address = (data & 0x3E) << 10;
					break;

				case 4:
					/* PATTERN NAME TABLE BASE ADDRESS FOR SCROLL B */
					vdp->state->plane_b_address = (data & 7) << 13;
					break;

				case 5:
					/* SPRITE ATTRIBUTE TABLE BASE ADDRESS */
					vdp->state->sprite_table_address = (data & 0x7F) << 9;

					/* Real VDPs partially cache the sprite table, and forget to update it
					   when the sprite table base address is changed. Replicating this
					   behaviour may be needed in order to properly emulate certain effects
					   that involve manipulating the sprite table during rendering. */
					/*vdp->state->sprite_row_cache.needs_updating = cc_true;*/ /* The VDP does not do this! */

					break;

				case 6:
					/* Unused legacy register for Master System mode. */
					/* TODO */
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

					/* TODO: External interrupt. */

					vdp->state->vscroll_mode = data & 4 ? VDP_VSCROLL_MODE_2CELL : VDP_VSCROLL_MODE_FULL;

					switch (data & 3)
					{
						case 0:
							vdp->state->hscroll_mode = VDP_HSCROLL_MODE_FULL;
							break;

						case 1:
							vdp->state->hscroll_mode = VDP_HSCROLL_MODE_INVALID;
							break;

						case 2:
							vdp->state->hscroll_mode = VDP_HSCROLL_MODE_1CELL;
							break;

						case 3:
							vdp->state->hscroll_mode = VDP_HSCROLL_MODE_1LINE;
							break;
					}

					break;

				case 12:
					/* MODE SET REGISTER NO.4 */
					/* TODO */
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
					vdp->state->hscroll_address = (data & 0x3F) << 10;
					break;

				case 14:
					/* PATTERN NAME TABLE BASE ADDRESS 128KiB */
					/* TODO */
					break;

				case 15:
					/* AUTO INCREMENT DATA */
					vdp->state->access.increment = (cc_u16l)data;
					break;

				case 16:
					/* SCROLL SIZE */
					/* https://gendev.spritesmind.net/forum/viewtopic.php?p=31307#p31307 */
					vdp->state->plane_height_bitmask = (data << 1) | 0x1F;

					switch (data & 3)
					{
						case 0:
							vdp->state->plane_width_bitmask = 0x1F;
							vdp->state->plane_height_bitmask &= 0x7F;
							break;

						case 1:
							vdp->state->plane_width_bitmask = 0x3F;
							vdp->state->plane_height_bitmask &= 0x3F;
							break;

						case 2:
							vdp->state->plane_width_bitmask = 0x1F;
							vdp->state->plane_height_bitmask &= 0;
							break;

						case 3:
							vdp->state->plane_width_bitmask = 0x7F;
							vdp->state->plane_height_bitmask &= 0x1F;
							break;
					}

					break;

				case 17:
					/* WINDOW H POSITION */
					vdp->state->window.aligned_right = (data & 0x80) != 0;
					vdp->state->window.horizontal_boundary = (data & 0x1F); /* Measured in tile pairs. */
					break;

				case 18:
					/* WINDOW V POSITION */
					vdp->state->window.aligned_bottom = (data & 0x80) != 0;
					vdp->state->window.vertical_boundary = (data & 0x1F) << GetTileHeightPower(vdp->state); /* Measured in tiles. */
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
				WriteVRAM(vdp->state, vdp->state->access.address_register ^ 1, vdp->state->vram[vdp->state->dma.source_address_low ^ 1]);
				vdp->state->access.address_register += vdp->state->access.increment;
			}

			/* Emulate the 128KiB DMA wrap-around bug. */
			++vdp->state->dma.source_address_low;
			vdp->state->dma.source_address_low &= 0xFFFF;
		} while (--vdp->state->dma.length, vdp->state->dma.length &= 0xFFFF, vdp->state->dma.length != 0);
	}
}

cc_u16f VDP_ReadVRAMWord(const VDP_State* const state, const cc_u16f address)
{
	assert(address < CC_COUNT_OF(state->vram));
	return (state->vram[address ^ 0] << 8) | state->vram[address ^ 1];
}

VDP_TileMetadata VDP_DecomposeTileMetadata(const cc_u16f packed_tile_metadata)
{
	VDP_TileMetadata tile_metadata;

	tile_metadata.tile_index = packed_tile_metadata & 0x7FF;
	tile_metadata.palette_line = (packed_tile_metadata >> 13) & 3;
	tile_metadata.x_flip = (packed_tile_metadata & 0x800) != 0;
	tile_metadata.y_flip = (packed_tile_metadata & 0x1000) != 0;
	tile_metadata.priority = (packed_tile_metadata & 0x8000) != 0;

	return tile_metadata;
}

VDP_CachedSprite VDP_GetCachedSprite(const VDP_State* const state, const cc_u16f sprite_index)
{
	VDP_CachedSprite cached_sprite;

	const cc_u8l* const bytes = state->sprite_table_cache[sprite_index];

	cached_sprite.y = (((bytes[0] & 3) << 8) | bytes[1]) & (0x3FF >> !state->double_resolution_enabled);
	cached_sprite.width = ((bytes[2] >> 2) & 3) + 1;
	cached_sprite.height = (bytes[2] & 3) + 1;
	cached_sprite.link = bytes[3] & 0x7F;

	return cached_sprite;
}
