#include "cdda.h"

#include <string.h>

#define CDDA_MAX_VOLUME 0x400
#define CDDA_VOLUME_MASK 0xFFF /* Sega's BIOS discards the upper 4 bits. */

void CDDA_Initialise(CDDA_State* const state)
{
	state->volume = CDDA_MAX_VOLUME;
	state->master_volume = CDDA_MAX_VOLUME;
	state->target_volume = 0;
	state->fade_step = 0;
	state->fade_remaining = 0;
	state->subtract_fade_step = cc_false;
	state->playing = cc_false;
	state->paused = cc_false;
}

void CDDA_Update(CDDA_State* const state, const CDDA_AudioReadCallback callback, const void* const user_data, cc_s16l* const sample_buffer, const size_t total_frames)
{
	const cc_u8f total_channels = 2;

	size_t frames_done = 0;
	size_t i;

	if (state->playing && !state->paused)
		frames_done = callback((void*)user_data, sample_buffer, total_frames);

	/* TODO: Add clamping if the volume is able to exceed 'CDDA_MAX_VOLUME'. */
	for (i = 0; i < frames_done * total_channels; ++i)
		sample_buffer[i] = (cc_s32f)sample_buffer[i] * state->volume / CDDA_MAX_VOLUME;

	/* Clear any samples that we could not read from the disc. */
	memset(sample_buffer + frames_done * total_channels, 0, (total_frames - frames_done) * sizeof(cc_s16l) * total_channels);
}

static cc_u16f ScaleByMasterVolume(CDDA_State* const state, const cc_u16f volume)
{
	/* TODO: What happens if the volume exceeds 'CDDA_MAX_VOLUME'? */
	return volume * state->master_volume / CDDA_MAX_VOLUME & CDDA_VOLUME_MASK;
}

void CDDA_SetVolume(CDDA_State* const state, const cc_u16f volume)
{
	/* Scale the volume by the master volume. */
	/* TODO: What happens if the volume exceeds 'CDDA_MAX_VOLUME'? */
	state->volume = ScaleByMasterVolume(state, volume);

	/* Halt any in-progress volume fade. */
	state->fade_remaining = 0;
}

void CDDA_SetMasterVolume(CDDA_State* const state, const cc_u16f master_volume)
{
	/* Unscale the volume by the old master volume... */
	const cc_u16f volume = state->volume * CDDA_MAX_VOLUME / state->master_volume;

	state->master_volume = master_volume;

	/* ...and then scale it by the new master volume. */
	CDDA_SetVolume(state, volume);
}

void CDDA_FadeToVolume(CDDA_State* const state, const cc_u16f target_volume, const cc_u16f fade_step)
{
	state->target_volume = ScaleByMasterVolume(state, target_volume);
	state->fade_step = fade_step;
	state->subtract_fade_step = target_volume < state->volume;

	if (state->subtract_fade_step)
		state->fade_remaining = state->volume - target_volume;
	else
		state->fade_remaining = target_volume - state->volume;
}

void CDDA_UpdateFade(CDDA_State* const state)
{
	if (state->fade_remaining == 0)
		return;

	state->fade_remaining -= CC_MIN(state->fade_remaining, state->fade_step);

	if (state->subtract_fade_step)
		state->volume = state->target_volume + state->fade_remaining;
	else
		state->volume = state->target_volume - state->fade_remaining;
}
