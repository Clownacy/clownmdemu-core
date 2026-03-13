#include "cdda.h"

#include <string.h>

#define CDDA_MAX_VOLUME 0x400
#define CDDA_VOLUME_MASK 0xFFF /* Sega's BIOS discards the upper 4 bits. */

void CDDA_Initialise(CDDA* const cdda)
{
	cdda->state.volume = CDDA_MAX_VOLUME;
	cdda->state.master_volume = CDDA_MAX_VOLUME;
	cdda->state.target_volume = 0;
	cdda->state.fade_step = 0;
	cdda->state.fade_remaining = 0;
	cdda->state.subtract_fade_step = cc_false;
	cdda->state.playing = cc_false;
	cdda->state.paused = cc_false;
}

void CDDA_Update(CDDA* const cdda, const CDDA_AudioReadCallback callback, const void* const user_data, cc_s16l* const sample_buffer, const size_t total_frames)
{
	const cc_u8f total_channels = 2;

	size_t frames_done = 0;
	size_t i;

	if (cdda->state.playing && !cdda->state.paused)
		frames_done = callback((void*)user_data, sample_buffer, total_frames);

	if (cdda->configuration.disabled)
		frames_done = 0;

	/* TODO: Add clamping if the volume is able to exceed 'CDDA_MAX_VOLUME'. */
	for (i = 0; i < frames_done * total_channels; ++i)
		sample_buffer[i] = (cc_s32f)sample_buffer[i] * cdda->state.volume / CDDA_MAX_VOLUME;

	/* Clear any samples that we could not read from the disc. */
	memset(sample_buffer + frames_done * total_channels, 0, (total_frames - frames_done) * sizeof(cc_s16l) * total_channels);
}

static cc_u16f ScaleByMasterVolume(CDDA* const cdda, const cc_u16f volume)
{
	/* TODO: What happens if the volume exceeds 'CDDA_MAX_VOLUME'? */
	return volume * cdda->state.master_volume / CDDA_MAX_VOLUME & CDDA_VOLUME_MASK;
}

void CDDA_SetVolume(CDDA* const cdda, const cc_u16f volume)
{
	/* Scale the volume by the master volume. */
	/* TODO: What happens if the volume exceeds 'CDDA_MAX_VOLUME'? */
	cdda->state.volume = ScaleByMasterVolume(cdda, volume);

	/* Halt any in-progress volume fade. */
	cdda->state.fade_remaining = 0;
}

void CDDA_SetMasterVolume(CDDA* const cdda, const cc_u16f master_volume)
{
	/* Unscale the volume by the old master volume... */
	const cc_u16f volume = cdda->state.volume * CDDA_MAX_VOLUME / cdda->state.master_volume;

	cdda->state.master_volume = master_volume;

	/* ...and then scale it by the new master volume. */
	CDDA_SetVolume(cdda, volume);
}

void CDDA_FadeToVolume(CDDA* const cdda, const cc_u16f target_volume, const cc_u16f fade_step)
{
	cdda->state.target_volume = ScaleByMasterVolume(cdda, target_volume);
	cdda->state.fade_step = fade_step;
	cdda->state.subtract_fade_step = target_volume < cdda->state.volume;

	if (cdda->state.subtract_fade_step)
		cdda->state.fade_remaining = cdda->state.volume - target_volume;
	else
		cdda->state.fade_remaining = target_volume - cdda->state.volume;
}

void CDDA_UpdateFade(CDDA* const cdda)
{
	if (cdda->state.fade_remaining == 0)
		return;

	cdda->state.fade_remaining -= CC_MIN(cdda->state.fade_remaining, cdda->state.fade_step);

	if (cdda->state.subtract_fade_step)
		cdda->state.volume = cdda->state.target_volume + cdda->state.fade_remaining;
	else
		cdda->state.volume = cdda->state.target_volume - cdda->state.fade_remaining;
}
