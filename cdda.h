#ifndef CDDA_H
#define CDDA_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

typedef size_t (*CDDA_AudioReadCallback)(void* user_data, cc_s16l* sample_buffer, size_t total_frames);

typedef struct CDDA
{
	cc_u16l volume, master_volume, target_volume, fade_step;
	cc_bool playing, paused;
} CDDA;

void CDDA_Initialise(CDDA *cdda);
void CDDA_Update(CDDA *cdda, CDDA_AudioReadCallback callback, const void* user_data, cc_s16l *sample_buffer, size_t total_frames);
#define CDDA_SetPlaying(CDDA, PLAYING) (CDDA)->playing = (PLAYING)
#define CDDA_SetPaused(CDDA, PAUSED) (CDDA)->paused = (PAUSED)

#endif /* CDDA_H */
