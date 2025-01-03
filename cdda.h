#ifndef CDDA_H
#define CDDA_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

typedef size_t (*CDDA_AudioReadCallback)(void* user_data, cc_s16l* sample_buffer, size_t total_frames);

typedef struct CDDA
{
	cc_u16l volume, master_volume, target_volume, fade_step, fade_remaining;
	cc_bool subtract_fade_step, playing, paused;
} CDDA;

void CDDA_Initialise(CDDA *cdda);
void CDDA_Update(CDDA *cdda, CDDA_AudioReadCallback callback, const void* user_data, cc_s16l *sample_buffer, size_t total_frames);
#define CDDA_SetPlaying(CDDA, PLAYING) (CDDA)->playing = (PLAYING)
#define CDDA_SetPaused(CDDA, PAUSED) (CDDA)->paused = (PAUSED)

void CDDA_SetVolume(CDDA *cdda, cc_u16f volume);
void CDDA_SetMasterVolume(CDDA *cdda, cc_u16f master_volume);
void CDDA_FadeToVolume(CDDA *cdda, cc_u16f target_volume, cc_u16f fade_step);
void CDDA_UpdateFade(CDDA *cdda);

#endif /* CDDA_H */
