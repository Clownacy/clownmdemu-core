#include "cdda.h"

#include <string.h>

#define CDDA_MAX_VOLUME 0x400
#define CDDA_VOLUME_MASK 0xFFF /* Sega's BIOS discards the upper 4 bits. */

void CDDA_Initialise(CDDA* const cdda)
{
	cdda->volume = CDDA_MAX_VOLUME;
	cdda->master_volume = CDDA_MAX_VOLUME;
	cdda->target_volume = 0;
	cdda->fade_step = 0;
	cdda->fade_remaining = 0;
	cdda->subtract_fade_step = cc_false;
	cdda->playing = cc_false;
	cdda->paused = cc_false;
}

void CDDA_Update(CDDA* const cdda, const CDDA_AudioReadCallback callback, const void* const user_data, cc_s16l* const sample_buffer, const size_t total_frames)
{
	const cc_u8f total_channels = 2;

	size_t frames_done = 0;
	size_t i;

	if (cdda->playing && !cdda->paused)
		frames_done = callback((void*)user_data, sample_buffer, total_frames);

	/* TODO: Add clamping if the volume is able to exceed 'CDDA_MAX_VOLUME'. */
	for (i = 0; i < frames_done * total_channels; ++i)
		sample_buffer[i] = (cc_u32f)sample_buffer[i] * cdda->volume / CDDA_MAX_VOLUME;

	/* Clear any samples that we could not read from the disc. */
	memset(sample_buffer + frames_done * total_channels, 0, (total_frames - frames_done) * sizeof(cc_s16l) * total_channels);
}

static cc_u16f ScaleByMasterVolume(CDDA* const cdda, const cc_u16f volume)
{
	/* TODO: What happens if the volume exceeds 'CDDA_MAX_VOLUME'? */
	return volume * cdda->master_volume / CDDA_MAX_VOLUME & CDDA_VOLUME_MASK;
}

void CDDA_SetVolume(CDDA* const cdda, const cc_u16f volume)
{
	/* Scale the volume by the master volume. */
	/* TODO: What happens if the volume exceeds 'CDDA_MAX_VOLUME'? */
	cdda->volume = ScaleByMasterVolume(cdda, volume);
}

void CDDA_SetMasterVolume(CDDA* const cdda, const cc_u16f master_volume)
{
	/* Unscale the volume by the old master volume... */
	const cc_u16f volume = cdda->volume * CDDA_MAX_VOLUME / cdda->master_volume;

	cdda->master_volume = master_volume;

	/* ...and then scale it by the new master volume. */
	CDDA_SetVolume(cdda, volume);
}

void CDDA_FadeToVolume(CDDA* const cdda, const cc_u16f target_volume, const cc_u16f fade_step)
{
	cdda->target_volume = ScaleByMasterVolume(cdda, target_volume);
	cdda->fade_step = fade_step;
	cdda->subtract_fade_step = target_volume < cdda->volume;
	if (cdda->subtract_fade_step)
		cdda->fade_remaining = cdda->volume - target_volume;
	else
		cdda->fade_remaining = target_volume - cdda->volume;
}

void CDDA_UpdateFade(CDDA* const cdda)
{
	if (cdda->fade_remaining == 0)
		return;

	if (cdda->fade_remaining < cdda->fade_step)
		cdda->fade_remaining = 0;
	else
		cdda->fade_remaining -= cdda->fade_step;

	if (cdda->subtract_fade_step)
		cdda->volume = cdda->target_volume + cdda->fade_remaining;
	else
		cdda->volume = cdda->target_volume - cdda->fade_remaining;
}
