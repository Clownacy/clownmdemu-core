#ifndef BUS_COMMON
#define BUS_COMMON

#include "clowncommon/clowncommon.h"

#include "clownmdemu.h"
#include "io-port.h"

typedef struct SyncState
{
	cc_u32f current_cycle;
} SyncState;

typedef struct SyncCPUState
{
	cc_u32f current_cycle;
	cc_u32l *cycle_countdown;
} SyncCPUState;

typedef struct CPUCallbackUserData
{
	const ClownMDEmu *clownmdemu;
	struct
	{
		SyncCPUState m68k;
		SyncCPUState z80;
		SyncCPUState mcd_m68k;
		SyncCPUState mcd_m68k_irq3;
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
	Controller *controller;
	const ClownMDEmu_Callbacks *frontend_callbacks;
	cc_u8f joypad_index;
} IOPortToController_Parameters;

typedef cc_u16f (*SyncCPUCommonCallback)(const ClownMDEmu *clownmdemu, void *user_data);

cc_u16f GetTelevisionVerticalResolution(const ClownMDEmu *clownmdemu);
CycleMegaDrive GetMegaDriveCyclesPerFrame(const ClownMDEmu *clownmdemu);

CycleMegaDrive MakeCycleMegaDrive(cc_u32f cycle);
CycleMegaCD MakeCycleMegaCD(cc_u32f cycle);
CycleMegaCD CycleMegaDriveToMegaCD(const ClownMDEmu *clownmdemu, CycleMegaDrive cycle);
CycleMegaDrive CycleMegaCDToMegaDrive(const ClownMDEmu *clownmdemu, CycleMegaCD cycle);

cc_u32f SyncCommon(SyncState *sync, cc_u32f target_cycle, cc_u32f clock_divisor);
void SyncCPUCommon(const ClownMDEmu *clownmdemu, SyncCPUState *sync, cc_u32f target_cycle, cc_bool cpu_not_running, SyncCPUCommonCallback callback, const void *user_data);
cc_u8f SyncFM(CPUCallbackUserData *other_state, CycleMegaDrive target_cycle);
void SyncPSG(CPUCallbackUserData *other_state, CycleMegaDrive target_cycle);
void SyncPCM(CPUCallbackUserData *other_state, CycleMegaCD target_cycle);
void SyncCDDA(CPUCallbackUserData *other_state, cc_u32f total_frames);

void RaiseHorizontalInterruptIfNeeded(const ClownMDEmu *clownmdemu);
void RaiseVerticalInterruptIfNeeded(const ClownMDEmu *clownmdemu);

#endif /* BUS_COMMON */
