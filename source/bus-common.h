#ifndef BUS_COMMON
#define BUS_COMMON

#include "../libraries/clowncommon/clowncommon.h"

#include "clownmdemu.h"
#include "io-port.h"

/* The cycle counts are from here:
   https://gendev.spritesmind.net/forum/viewtopic.php?t=3058
   https://gendev.spritesmind.net/forum/viewtopic.php?t=768 */

/* The refresh cycle counts are from here:
   http://gendev.spritesmind.net/forum/viewtopic.php?p=20921#p20921 */

enum
{
	VDP_CYCLES_H40_LEFT_BLANKING_1 = 2,
	VDP_CYCLES_H40_LEFT_BLANKING_2 = 62,
	VDP_CYCLES_H40_LEFT_BORDER = 26,
	VDP_CYCLES_H40_ACTIVE_DISPLAY = 640,
	VDP_CYCLES_H40_RIGHT_BORDER = 28,
	VDP_CYCLES_H40_RIGHT_BLANKING = 18,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_1 = 15,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_2 = 2,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_3 = 15,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_4 = 2,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_5 = 15,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_6 = 2,
	VDP_CYCLES_H40_HORIZONTAL_SYNC_7 = 13,

	VDP_CYCLES_H40_PER_RASTER_LINE = VDP_CYCLES_H40_LEFT_BLANKING_1 + VDP_CYCLES_H40_LEFT_BLANKING_2 + VDP_CYCLES_H40_LEFT_BORDER + VDP_CYCLES_H40_ACTIVE_DISPLAY + VDP_CYCLES_H40_RIGHT_BORDER + VDP_CYCLES_H40_RIGHT_BLANKING,
	VDP_CYCLES_H40_PER_HORIZONTAL_SYNC = VDP_CYCLES_H40_HORIZONTAL_SYNC_1 + VDP_CYCLES_H40_HORIZONTAL_SYNC_2 + VDP_CYCLES_H40_HORIZONTAL_SYNC_3 + VDP_CYCLES_H40_HORIZONTAL_SYNC_4 + VDP_CYCLES_H40_HORIZONTAL_SYNC_5 + VDP_CYCLES_H40_HORIZONTAL_SYNC_6 + VDP_CYCLES_H40_HORIZONTAL_SYNC_7,
	VDP_CYCLES_H40_PER_LINE = VDP_CYCLES_H40_PER_RASTER_LINE + VDP_CYCLES_H40_PER_HORIZONTAL_SYNC,

	VDP_REFRESH_CYCLES_H40_DISPLAY_OFF = 6,

	VDP_H40_DMA_BYTES_PER_LINE_DISPLAY_ON = 18, /* TODO: Figure out how this number is determined. */
	VDP_H40_DMA_BYTES_PER_LINE_DISPLAY_OFF = (VDP_CYCLES_H40_PER_LINE / 4) - VDP_REFRESH_CYCLES_H40_DISPLAY_OFF,

	VDP_CYCLES_H32_LEFT_BLANKING = 48,
	VDP_CYCLES_H32_LEFT_BORDER = 26,
	VDP_CYCLES_H32_ACTIVE_DISPLAY = 512,
	VDP_CYCLES_H32_RIGHT_BORDER = 28,
	VDP_CYCLES_H32_RIGHT_BLANKING = 18,
	VDP_CYCLES_H32_HORIZONTAL_SYNC = 52,

	VDP_CYCLES_H32_PER_RASTER_LINE = VDP_CYCLES_H32_LEFT_BLANKING + VDP_CYCLES_H32_LEFT_BORDER + VDP_CYCLES_H32_ACTIVE_DISPLAY + VDP_CYCLES_H32_RIGHT_BORDER + VDP_CYCLES_H32_RIGHT_BLANKING,
	VDP_CYCLES_H32_PER_HORIZONTAL_SYNC = VDP_CYCLES_H32_HORIZONTAL_SYNC,
	VDP_CYCLES_H32_PER_LINE = VDP_CYCLES_H32_PER_RASTER_LINE + VDP_CYCLES_H32_PER_HORIZONTAL_SYNC,

	VDP_REFRESH_CYCLES_H32_DISPLAY_ON = 4,
	VDP_REFRESH_CYCLES_H32_DISPLAY_OFF = 5,

	VDP_H32_DMA_BYTES_PER_LINE_DISPLAY_ON = 16, /* TODO: Figure out how this number is determined. */
	VDP_H32_DMA_BYTES_PER_LINE_DISPLAY_OFF = (VDP_CYCLES_H32_PER_LINE / 4) - VDP_REFRESH_CYCLES_H32_DISPLAY_OFF,

	VDP_LINES_BOTTOM_BLANKING = 3,
	VDP_LINES_VERTICAL_SYNC = 3,
	VDP_LINES_TOP_BLANKING = 13
};
typedef struct SyncState
{
	cc_u32f current_cycle;
} SyncState;

typedef struct SyncCPUState
{
	cc_u32f current_cycle;
	cc_u32l *cycle_countdown;
} SyncCPUState;

typedef struct SyncM68kState
{
	cc_u32f current_cycle;
	cc_u32f base_cycle;
} SyncM68kState;

typedef struct CPUCallbackUserData
{
	ClownMDEmu *clownmdemu;
	struct
	{
		SyncM68kState m68k;
		SyncCPUState z80;
		SyncM68kState mcd_m68k;
		SyncCPUState mcd_m68k_irq3;
		SyncCPUState vdp_dma_transfer;
		SyncState fm;
		SyncState psg;
		SyncState pcm;
		SyncState io_ports[3];
	} sync;
} CPUCallbackUserData;

typedef struct CycleMegaDrive
{
	cc_u32f cycle;
} CycleMegaDrive;

typedef struct CycleMegaCD
{
	cc_u32f cycle;
} CycleMegaCD;

/* TODO: Move this to somewhere more specific. */
typedef struct IOPortToController_Parameters
{
	ClownMDEmu *clownmdemu;
	cc_u8f joypad_index;
} IOPortToController_Parameters;

typedef cc_u16f (*SyncCPUCommonCallback)(ClownMDEmu *clownmdemu, void *user_data);

cc_u16f GetTelevisionVerticalResolution(const ClownMDEmu *clownmdemu);
CycleMegaDrive GetMegaDriveCyclesPerFrame(const ClownMDEmu *clownmdemu);
CycleMegaDrive GetMegaDriveCyclesPerScanline(const ClownMDEmu *clownmdemu);

CycleMegaDrive MakeCycleMegaDrive(cc_u32f cycle);
CycleMegaCD MakeCycleMegaCD(cc_u32f cycle);
CycleMegaCD CycleMegaDriveToMegaCD(const ClownMDEmu *clownmdemu, CycleMegaDrive cycle);
CycleMegaDrive CycleMegaCDToMegaDrive(const ClownMDEmu *clownmdemu, CycleMegaCD cycle);

cc_u32f SyncCommon(SyncState *sync, cc_u32f target_cycle, cc_u32f clock_divisor);
void SyncCPUCommon(ClownMDEmu *clownmdemu, SyncCPUState *sync, cc_u32f target_cycle, cc_bool cpu_not_running, SyncCPUCommonCallback callback, const void *user_data);
cc_u8f SyncFM(CPUCallbackUserData *other_state, CycleMegaDrive target_cycle);
void SyncPSG(CPUCallbackUserData *other_state, CycleMegaDrive target_cycle);
void SyncPCM(CPUCallbackUserData *other_state, CycleMegaCD target_cycle);
void SyncCDDA(CPUCallbackUserData *other_state, cc_u32f total_frames);

void RaiseHorizontalInterruptIfNeeded(ClownMDEmu *clownmdemu);
void RaiseVerticalInterruptIfNeeded(ClownMDEmu *clownmdemu);

#endif /* BUS_COMMON */
