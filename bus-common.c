#include "bus-common.h"

#include <assert.h>

#include "cdda.h"
#include "fm.h"
#include "low-pass-filter.h"
#include "pcm.h"
#include "psg.h"

cc_u16f GetTelevisionVerticalResolution(const ClownMDEmu* const clownmdemu)
{
	return clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? 312 : 262; /* PAL and NTSC, respectively */
}

CycleMegaDrive GetMegaDriveCyclesPerFrame(const ClownMDEmu* const clownmdemu)
{
	return MakeCycleMegaDrive(clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_PAL ? CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_MASTER_CLOCK_PAL) : CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_MASTER_CLOCK_NTSC));
}

CycleMegaDrive MakeCycleMegaDrive(const cc_u32f cycle)
{
	CycleMegaDrive cycle_mega_drive;
	cycle_mega_drive.cycle = cycle;
	return cycle_mega_drive;
}

CycleMegaCD MakeCycleMegaCD(const cc_u32f cycle)
{
	CycleMegaCD cycle_mega_cd;
	cycle_mega_cd.cycle = cycle;
	return cycle_mega_cd;
}

static cc_u32f ConvertCycle(const cc_u32f cycle, const cc_u32f* const scale_halves)
{
	const cc_u32f cycle_upper = cycle >> 16;
	const cc_u32f cycle_lower = cycle & 0xFFFF;

	const cc_u32f result_upper = cycle_upper * scale_halves[0];
	const cc_u32f result_lower1 = cycle_upper * scale_halves[1];
	const cc_u32f result_lower2 = cycle_lower * scale_halves[0];

	const cc_u32f result = (result_upper << 1) + (result_lower1 >> 15) + (result_lower2 >> 15);

	assert(cycle <= 0xFFFFFFFF);

	return result;
}

CycleMegaCD CycleMegaDriveToMegaCD(const ClownMDEmu* const clownmdemu, const CycleMegaDrive cycle)
{
	/* These are 32-bit integers split in half. */
	const cc_u32f ntsc[2] = {0x7732, 0x1ECA}; /* 0x80000000 * CLOWNMDEMU_MCD_MASTER_CLOCK / CLOWNMDEMU_MASTER_CLOCK_NTSC */
	const cc_u32f pal[2] = {0x784B, 0x02AF}; /* 0x80000000 * CLOWNMDEMU_MCD_MASTER_CLOCK / CLOWNMDEMU_MASTER_CLOCK_PAL */

	CycleMegaCD new_cycle;
	new_cycle.cycle = ConvertCycle(cycle.cycle, clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_NTSC ? ntsc : pal);
	return new_cycle;
}

CycleMegaDrive CycleMegaCDToMegaDrive(const ClownMDEmu* const clownmdemu, const CycleMegaCD cycle)
{
	/* These are 32-bit integers split in half. */
	const cc_u32f ntsc[2] = {0x8974, 0x5BF2}; /* 0x80000000 * CLOWNMDEMU_MASTER_CLOCK_NTSC / CLOWNMDEMU_MCD_MASTER_CLOCK */
	const cc_u32f pal[2] = {0x8833, 0x655D}; /* 0x80000000 * CLOWNMDEMU_MASTER_CLOCK_PAL / CLOWNMDEMU_MCD_MASTER_CLOCK */

	CycleMegaDrive new_cycle;
	new_cycle.cycle = ConvertCycle(cycle.cycle, clownmdemu->configuration->general.tv_standard == CLOWNMDEMU_TV_STANDARD_NTSC ? ntsc : pal);
	return new_cycle;
}

cc_u32f SyncCommon(SyncState* const sync, const cc_u32f target_cycle, const cc_u32f clock_divisor)
{
	const cc_u32f native_target_cycle = target_cycle / clock_divisor;

	const cc_u32f cycles_to_do = native_target_cycle - sync->current_cycle;

	assert(native_target_cycle >= sync->current_cycle); /* If this fails, then we must have failed to synchronise somewhere! */

	sync->current_cycle = native_target_cycle;

	return cycles_to_do;
}

void SyncCPUCommon(const ClownMDEmu* const clownmdemu, SyncCPUState* const sync, const cc_u32f target_cycle, const cc_bool cpu_not_running, const SyncCPUCommonCallback callback, const void* const user_data)
{
	/* Store this in a local variable to make the upcoming code faster. */
	cc_u16f countdown = *sync->cycle_countdown;

	if (countdown == 0 || cpu_not_running)
	{
		sync->current_cycle = target_cycle;
	}
	else
	{
		while (sync->current_cycle < target_cycle)
		{
			const cc_u32f cycles_to_do = CC_MIN(countdown, target_cycle - sync->current_cycle);

			sync->current_cycle += cycles_to_do;

			countdown -= cycles_to_do;

			if (countdown == 0)
				countdown = callback(clownmdemu, (void*)user_data);
		}

		/* Store this back in memory for later. */
		*sync->cycle_countdown = countdown;
	}
}

static void FMCallbackWrapper(const ClownMDEmu* const clownmdemu, cc_s16l* const sample_buffer, const size_t total_frames)
{
	FM_OutputSamples(&clownmdemu->state->fm, sample_buffer, total_frames);

	/* https://www.meme.net.au/butterworth.html
	   Configured for a cut-off of 2842Hz at 53267Hz.
	   53267Hz is the YM2612's sample rate.
	   2842Hz is the cut-off frequency of a VA4 Mega Drive's low-pass filter,
	   which is implemented as an RC filter with a 10K resistor and a 5600pf capacitor.
	   2842 = 1 / (2 * pi * (10 * (10 ^ 3)) * (5600 * (10 ^ -12))) */
	/* TODO: PAL frequency. */
	if (!clownmdemu->configuration->general.low_pass_filter_disabled)
		LowPassFilter_FirstOrder_Apply(clownmdemu->state->low_pass_filters.fm, CC_COUNT_OF(clownmdemu->state->low_pass_filters.fm), sample_buffer, total_frames, LOW_PASS_FILTER_COMPUTE_MAGIC_FIRST_ORDER(6.910, 4.910));
}

static void GenerateFMAudio(const void* const user_data, const cc_u32f total_frames)
{
	CPUCallbackUserData* const callback_user_data = (CPUCallbackUserData*)user_data;

	callback_user_data->clownmdemu->callbacks->fm_audio_to_be_generated((void*)callback_user_data->clownmdemu->callbacks->user_data, callback_user_data->clownmdemu, total_frames, FMCallbackWrapper);
}

cc_u8f SyncFM(CPUCallbackUserData* const other_state, const CycleMegaDrive target_cycle)
{
	return FM_Update(&other_state->clownmdemu->state->fm, SyncCommon(&other_state->sync.fm, target_cycle.cycle, CLOWNMDEMU_M68K_CLOCK_DIVIDER), GenerateFMAudio, other_state);
}

static void GeneratePSGAudio(const ClownMDEmu* const clownmdemu, cc_s16l* const sample_buffer, const size_t total_frames)
{
	PSG_Update(&clownmdemu->state->psg, sample_buffer, total_frames);

	/* https://www.meme.net.au/butterworth.html
	   Configured for a cut-off of 2842Hz at 223722Hz.
	   223722Hz is the SN76489's sample rate.
	   2842Hz is the cut-off frequency of a VA4 Mega Drive's low-pass filter,
	   which is implemented as an RC filter with a 10K resistor and a 5600pf capacitor.
	   2842 = 1 / (2 * pi * (10 * (10 ^ 3)) * (5600 * (10 ^ -12))) */
	/* TODO: PAL frequency. */
	if (!clownmdemu->configuration->general.low_pass_filter_disabled)
		LowPassFilter_FirstOrder_Apply(clownmdemu->state->low_pass_filters.psg, CC_COUNT_OF(clownmdemu->state->low_pass_filters.psg), sample_buffer, total_frames, LOW_PASS_FILTER_COMPUTE_MAGIC_FIRST_ORDER(26.044, 24.044));
}

void SyncPSG(CPUCallbackUserData* const other_state, const CycleMegaDrive target_cycle)
{
	const cc_u32f frames_to_generate = SyncCommon(&other_state->sync.psg, target_cycle.cycle, CLOWNMDEMU_Z80_CLOCK_DIVIDER * CLOWNMDEMU_PSG_SAMPLE_RATE_DIVIDER);

	/* TODO: Is this check necessary? */
	if (frames_to_generate != 0)
		other_state->clownmdemu->callbacks->psg_audio_to_be_generated((void*)other_state->clownmdemu->callbacks->user_data, other_state->clownmdemu, frames_to_generate, GeneratePSGAudio);
}

static void GeneratePCMAudio(const ClownMDEmu* const clownmdemu, cc_s16l* const sample_buffer, const size_t total_frames)
{
	PCM_Update(&clownmdemu->state->mega_cd.pcm, sample_buffer, total_frames);

	/* https://www.meme.net.au/butterworth.html
	   Configured for a cut-off of 7973Hz at 32552Hz.
	   32552Hz is the RF5C164's sample rate.
	   7973Hz is the cut-off frequency of a Mega CD's PCM low-pass filter. */
	/* TODO: Verify this against the Mega CD's schematic. */
	if (!clownmdemu->configuration->general.low_pass_filter_disabled)
		LowPassFilter_SecondOrder_Apply(clownmdemu->state->low_pass_filters.pcm, CC_COUNT_OF(clownmdemu->state->low_pass_filters.pcm), sample_buffer, total_frames, LOW_PASS_FILTER_COMPUTE_MAGIC_SECOND_ORDER(3.526, 0.132, 0.606));
}

void SyncPCM(CPUCallbackUserData* const other_state, const CycleMegaCD target_cycle)
{
	other_state->clownmdemu->callbacks->pcm_audio_to_be_generated((void*)other_state->clownmdemu->callbacks->user_data, other_state->clownmdemu, SyncCommon(&other_state->sync.pcm, target_cycle.cycle, CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER * CLOWNMDEMU_PCM_SAMPLE_RATE_DIVIDER), GeneratePCMAudio);
}

static void GenerateCDDAAudio(const ClownMDEmu* const clownmdemu, cc_s16l* const sample_buffer, const size_t total_frames)
{
	CDDA_Update(&clownmdemu->state->mega_cd.cdda, clownmdemu->callbacks->cd_audio_read, clownmdemu->callbacks->user_data, sample_buffer, total_frames);
}

void SyncCDDA(CPUCallbackUserData* const other_state, const cc_u32f total_frames)
{
	other_state->clownmdemu->callbacks->cdda_audio_to_be_generated((void*)other_state->clownmdemu->callbacks->user_data, other_state->clownmdemu, total_frames, GenerateCDDAAudio);
}

/* https://gendev.spritesmind.net/forum/viewtopic.php?t=3290 */

void RaiseHorizontalInterruptIfNeeded(const ClownMDEmu* const clownmdemu)
{
	if (clownmdemu->state->m68k.h_int_pending && clownmdemu->state->vdp.state.h_int_enabled)
	{
		clownmdemu->state->m68k.h_int_pending = cc_false;

		Clown68000_Interrupt(&clownmdemu->state->m68k.state, 4);
	}
}

void RaiseVerticalInterruptIfNeeded(const ClownMDEmu* const clownmdemu)
{
	if (clownmdemu->state->m68k.v_int_pending && clownmdemu->state->vdp.state.v_int_enabled)
	{
		clownmdemu->state->m68k.v_int_pending = cc_false;

		Clown68000_Interrupt(&clownmdemu->state->m68k.state, 6);
	}
}
