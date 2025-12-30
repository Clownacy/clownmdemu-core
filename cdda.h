#ifndef CDDA_H
#define CDDA_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

typedef size_t (*CDDA_AudioReadCallback)(void* user_data, cc_s16l* sample_buffer, size_t total_frames);

typedef struct CDDA_State
{
	cc_u16l volume, master_volume, target_volume, fade_step, fade_remaining;
	cc_bool subtract_fade_step, playing, paused;
} CDDA_State;

void CDDA_Initialise(CDDA_State *state);
void CDDA_Update(CDDA_State *state, CDDA_AudioReadCallback callback, const void* user_data, cc_s16l *sample_buffer, size_t total_frames);
#define CDDA_SetPlaying(STATE, PLAYING) (STATE)->playing = (PLAYING)
#define CDDA_SetPaused(STATE, PAUSED) (STATE)->paused = (PAUSED)

void CDDA_SetVolume(CDDA_State *state, cc_u16f volume);
void CDDA_SetMasterVolume(CDDA_State *state, cc_u16f master_volume);
void CDDA_FadeToVolume(CDDA_State *state, cc_u16f target_volume, cc_u16f fade_step);
void CDDA_UpdateFade(CDDA_State *state);

#endif /* CDDA_H */
