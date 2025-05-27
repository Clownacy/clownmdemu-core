#ifndef VDP_H
#define VDP_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VDP_MAX_SCANLINE_WIDTH 320
#define VDP_MAX_SCANLINES (240 * 2) /* V30 in interlace mode 2 */

typedef struct VDP_Configuration
{
	cc_bool sprites_disabled;
	cc_bool window_disabled;
	cc_bool planes_disabled[2];
} VDP_Configuration;

typedef struct VDP_Constant
{
	cc_u8l blit_lookup[1 << (1 + 2)][1 << (1 + 1 + 2 + 4)][1 << 4];
	cc_u8l blit_lookup_shadow_highlight[1 << (1 + 2)][1 << (1 + 1 + 2 + 4)][1 << 4];
} VDP_Constant;

typedef enum VDP_Access
{
	VDP_ACCESS_VRAM,
	VDP_ACCESS_CRAM,
	VDP_ACCESS_VSRAM,
	VDP_ACCESS_VRAM_8BIT,
	VDP_ACCESS_INVALID
} VDP_Access;

typedef enum VDP_DMAMode
{
	VDP_DMA_MODE_MEMORY_TO_VRAM, /* TODO: This isn't limited to VRAM. */
	VDP_DMA_MODE_FILL,
	VDP_DMA_MODE_COPY
} VDP_DMAMode;

typedef enum VDP_HScrollMode
{
	VDP_HSCROLL_MODE_FULL,
	VDP_HSCROLL_MODE_INVALID,
	VDP_HSCROLL_MODE_1CELL,
	VDP_HSCROLL_MODE_1LINE
} VDP_HScrollMode;

typedef enum VDP_VScrollMode
{
	VDP_VSCROLL_MODE_FULL,
	VDP_VSCROLL_MODE_2CELL
} VDP_VScrollMode;

typedef struct VDP_TileMetadata
{
	cc_u16f tile_index;
	cc_u8f palette_line;
	cc_bool x_flip;
	cc_bool y_flip;
	cc_bool priority;
} VDP_TileMetadata;

typedef struct VDP_CachedSprite
{
	cc_u16f y;
	cc_u8f link;
	cc_u8f width;
	cc_u8f height;
} VDP_CachedSprite;

typedef struct VDP_SpriteRowCacheEntry
{
	cc_u8l table_index;
	cc_u8l y_in_sprite;
	cc_u8l width;
	cc_u8l height;
} VDP_SpriteRowCacheEntry;

typedef struct VDP_SpriteRowCacheRow
{
	cc_u8l total;
	VDP_SpriteRowCacheEntry sprites[20];
} VDP_SpriteRowCacheRow;

typedef struct VDP_State
{
	struct
	{
		cc_bool write_pending;
		cc_u32l address_register;
		cc_u16l code_register;
		cc_u8l increment;

		VDP_Access selected_buffer;
	} access;

	struct
	{
		cc_bool enabled;
		VDP_DMAMode mode;
		cc_u8l source_address_high;
		cc_u16l source_address_low;
		cc_u16l length;
	} dma;

	cc_u16l plane_a_address;
	cc_u16l plane_b_address;
	cc_u16l window_address;
	cc_u16l sprite_table_address;
	cc_u16l hscroll_address;

	struct
	{
		cc_bool aligned_right;
		cc_bool aligned_bottom;
		cc_u16l horizontal_boundary;
		cc_u16l vertical_boundary;
	} window;

	cc_u8l plane_width_shift;
	cc_u8l plane_height_bitmask;

	cc_bool display_enabled;
	cc_bool v_int_enabled;
	cc_bool h_int_enabled;
	cc_bool h40_enabled;
	cc_bool v30_enabled;
	cc_bool mega_drive_mode_enabled;
	cc_bool shadow_highlight_enabled;
	cc_bool double_resolution_enabled;

	cc_u8l background_colour;
	cc_u8l h_int_interval;
	cc_bool currently_in_vblank;
	cc_bool allow_sprite_masking;

	cc_u8l hscroll_mask;
	VDP_VScrollMode vscroll_mode;

	cc_bool dma_cycle_delay;

	cc_u8l vram[0x10000];
	cc_u16l cram[4 * 16];
	/* http://gendev.spritesmind.net/forum/viewtopic.php?p=36727#p36727 */
	/* According to Mask of Destiny on SpritesMind, later models of Mega Drive (MD2 VA4 and later) have 64 words
	   of VSRAM, instead of the 40 words that earlier models have. */
	/* TODO: Add a toggle for Model 1 and Model 2 behaviour. */
	cc_u16l vsram[64];

	cc_u8l sprite_table_cache[80][4];

	struct
	{
		cc_bool needs_updating;
		VDP_SpriteRowCacheRow rows[VDP_MAX_SCANLINES];
	} sprite_row_cache;

	/* A placeholder for the FIFO, needed for CRAM/VSRAM DMA fills. */
	/* TODO: Implement the actual VDP FIFO. */
	cc_u16l previous_data_writes[4];

	/* Gens KMod's custom debug register 30. */
	cc_u16l kdebug_buffer_index;
	char kdebug_buffer[0x100];
} VDP_State;

typedef struct VDP
{
	const VDP_Configuration *configuration;
	const VDP_Constant *constant;
	VDP_State *state;
} VDP;

typedef void (*VDP_ScanlineRenderedCallback)(void *user_data, cc_u16f scanline, const cc_u8l *pixels, cc_u16f left_boundary, cc_u16f right_boundary, cc_u16f screen_width, cc_u16f screen_height);
typedef void (*VDP_ColourUpdatedCallback)(void *user_data, cc_u16f index, cc_u16f colour);
typedef cc_u16f (*VDP_ReadCallback)(void *user_data, cc_u32f address);
typedef void (*VDP_KDebugCallback)(void *user_data, const char *string);

void VDP_Constant_Initialise(VDP_Constant *constant);
void VDP_State_Initialise(VDP_State *state);
void VDP_RenderScanline(const VDP *vdp, cc_u16f scanline, VDP_ScanlineRenderedCallback scanline_rendered_callback, const void *scanline_rendered_callback_user_data);

cc_u16f VDP_ReadData(const VDP *vdp);
cc_u16f VDP_ReadControl(const VDP *vdp);
void VDP_WriteData(const VDP *vdp, cc_u16f value, VDP_ColourUpdatedCallback colour_updated_callback, const void *colour_updated_callback_user_data);
void VDP_WriteControl(const VDP *vdp, cc_u16f value, VDP_ColourUpdatedCallback colour_updated_callback, const void *colour_updated_callback_user_data, VDP_ReadCallback read_callback, const void *read_callback_user_data, VDP_KDebugCallback kdebug_callback, const void *kdebug_callback_user_data);

cc_u16f VDP_ReadVRAMWord(const VDP_State *state, cc_u16f address);
VDP_TileMetadata VDP_DecomposeTileMetadata(cc_u16f packed_tile_metadata);
VDP_CachedSprite VDP_GetCachedSprite(const VDP_State *state, cc_u16f sprite_index);
#define VDP_GetTileIndex(metadata) ((metadata) & 0x7FF)
#define VDP_GetTilePaletteLine(metadata) (((metadata) >> 13) & 3)
#define VDP_GetTileXFlip(metadata) (((metadata) & 0x800) != 0)
#define VDP_GetTileYFlip(metadata) (((metadata) & 0x1000) != 0)
#define VDP_GetTilePriority(metadata) (((metadata) & 0x8000) != 0)

#ifdef __cplusplus
}
#endif

#endif /* VDP_H */
