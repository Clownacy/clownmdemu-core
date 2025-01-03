#include "cdda.h"

#include <string.h>

#define CDDA_MAX_VOLUME 0x400

void CDDA_Initialise(CDDA* const cdda)
{
	cdda->volume = CDDA_MAX_VOLUME;
	cdda->master_volume = CDDA_MAX_VOLUME;
	cdda->target_volume = 0;
	cdda->fade_step = 0;
	cdda->playing = cc_false;
	cdda->paused = cc_false;
}

void CDDA_Update(CDDA* const cdda, const CDDA_AudioReadCallback callback, const void* const user_data, cc_s16l* const sample_buffer, const size_t total_frames)
{
	const cc_u8f total_channels = 2;

	size_t frames_done = 0;

	if (cdda->playing && !cdda->paused)
		frames_done = callback((void*)user_data, sample_buffer, total_frames);

	/* Clear any samples that we could not read from the disc. */
	memset(sample_buffer + frames_done * total_channels, 0, (total_frames - frames_done) * sizeof(cc_s16l) * total_channels);
}
