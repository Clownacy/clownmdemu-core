/*
PSG emulator

This webpage has been an invaluable resource:
https://www.smspower.org/Development/SN76489
*/

#include "psg.h"

#include <math.h>

#include "clowncommon/clowncommon.h"

static const cc_s16l psg_volumes[0x10][2] = {
	{0x1FFF, -0x1FFF},
	{0x196A, -0x196A},
	{0x1430, -0x1430},
	{0x1009, -0x1009},
	{0x0CBD, -0x0CBD},
	{0x0A1E, -0x0A1E},
	{0x0809, -0x0809},
	{0x0662, -0x0662},
	{0x0512, -0x0512},
	{0x0407, -0x0407},
	{0x0333, -0x0333},
	{0x028A, -0x028A},
	{0x0204, -0x0204},
	{0x019A, -0x019A},
	{0x0146, -0x0146},
	{0x0000, -0x0000}
};

#if 0
void PSG_Constant_Initialise(void)
{
	cc_u8f i;

	/* Generate the volume lookup table. */
	for (i = 0; i < CC_COUNT_OF(psg_volumes) - 1; ++i)
	{
		/* Each volume level is 2 decibels lower than the last. */
		/* The division by 4 is because there are 4 channels, so we want to prevent audio clipping. */
		const cc_s16l volume = (cc_s16l)(((double)0x7FFF / 4.0) * pow(10.0, -2.0 * (double)i / 20.0));

		psg_volumes[i][0] = volume; /* Positive phase. */
		psg_volumes[i][1] = -volume; /* Negative phase. */
	}

	/* The lowest volume is always 0. */
	psg_volumes[CC_COUNT_OF(psg_volumes) - 1][0] = 0;
	psg_volumes[CC_COUNT_OF(psg_volumes) - 1][1] = 0;
}
#endif

void PSG_State_Initialise(PSG_State* const state)
{
	size_t i;

	/* Reset tone channels. */
	for (i = 0; i < CC_COUNT_OF(state->tones); ++i)
	{
		state->tones[i].countdown = 0;
		state->tones[i].countdown_master = 0;
		state->tones[i].attenuation = 0xF; /* Silence the channels on startup. */
		state->tones[i].output_bit = 0;
	}

	/* Reset noise channel. */
	state->noise.countdown = 0;
	state->noise.attenuation = 0xF;
	state->noise.fake_output_bit = 0;
	state->noise.real_output_bit = 0;
	state->noise.frequency_mode = 0;
	state->noise.type = PSG_NOISE_TYPE_PERIODIC;
	state->noise.shift_register = 0;

	/* Reset the latched command data. */
	state->latched_command.channel = 0;
	state->latched_command.is_volume_command = cc_false;
}

void PSG_DoCommand(const PSG* const psg, const cc_u8f command)
{
	const cc_bool latch = (command & 0x80) != 0;

	if (latch)
	{
		/* Latch command. */
		psg->state->latched_command.channel = (command >> 5) & 3;
		psg->state->latched_command.is_volume_command = (command & 0x10) != 0;
	}

	if (psg->state->latched_command.channel < CC_COUNT_OF(psg->state->tones))
	{
		/* Tone channel. */
		PSG_ToneState* const tone = &psg->state->tones[psg->state->latched_command.channel];

		if (psg->state->latched_command.is_volume_command)
		{
			/* Volume attenuation command. */
			tone->attenuation = command & 0xF;
			/* According to http://md.railgun.works/index.php?title=PSG, this should happen,
			   but when I test it, I get crackly audio, so I've disabled it for now. */
			/*tone->output_bit = 0;*/
		}
		else
		{
			/* Frequency command. */
			if (latch)
			{
				/* Low frequency bits. */
				tone->countdown_master &= ~0xF;
				tone->countdown_master |= command & 0xF;
			}
			else
			{
				/* High frequency bits. */
				tone->countdown_master &= 0xF;
				tone->countdown_master |= (command & 0x3F) << 4;
			}
		}
	}
	else
	{
		/* Noise channel. */
		if (psg->state->latched_command.is_volume_command)
		{
			/* Volume attenuation command. */
			psg->state->noise.attenuation = command & 0xF;
			/* According to http://md.railgun.works/index.php?title=PSG, this should happen,
			   but when I test it, I get crackly audio, so I've disabled it for now. */
			/*state->noise.fake_output_bit = 0;*/
		}
		else
		{
			/* Frequency and noise type command. */
			psg->state->noise.type = (command & 4) ? PSG_NOISE_TYPE_WHITE : PSG_NOISE_TYPE_PERIODIC;
			psg->state->noise.frequency_mode = command & 3;

			/* https://www.smspower.org/Development/SN76489
			   "When the noise register is written to, the shift register is reset,
			   such that all bits are zero except for the highest bit. This will make
			   the "periodic noise" output a 1/16th (or 1/15th) duty cycle, and is
			   important as it also affects the sound of white noise." */
			psg->state->noise.shift_register = 1;
		}
	}
}

void PSG_Update(const PSG* const psg, cc_s16l* const sample_buffer, const size_t total_frames)
{
	size_t i;
	size_t j;
	cc_s16l *sample_buffer_pointer;

	/* Do the tone channels. */
	for (i = 0; i < CC_COUNT_OF(psg->state->tones); ++i)
	{
		if (!psg->configuration->tone_disabled[i])
		{
			PSG_ToneState* const tone = &psg->state->tones[i];

			sample_buffer_pointer = sample_buffer;

			for (j = 0; j < total_frames; ++j)
			{
				/* This countdown is responsible for the channel's frequency. */
				if (tone->countdown != 0)
					--tone->countdown;

				/* Curiously, the phase never changes if the frequency is at its maximum.
				   This can be exploited to play PCM samples. After Burner II relies on this. */
				if (tone->countdown_master != 0 && tone->countdown == 0)
				{
					/* Reset the countdown. */
					tone->countdown = tone->countdown_master;

					/* Switch from positive phase to negative phase and vice versa. */
					tone->output_bit = !tone->output_bit;
				}

				/* Output a sample. */
				*sample_buffer_pointer++ += psg_volumes[tone->attenuation][tone->output_bit];
			}
		}
	}

	if (!psg->configuration->noise_disabled)
	{
		/* Do the noise channel. */
		sample_buffer_pointer = sample_buffer;

		for (j = 0; j < total_frames; ++j)
		{
			/* This countdown is responsible for the channel's frequency. */
			if (psg->state->noise.countdown != 0)
				--psg->state->noise.countdown;

			if (psg->state->noise.countdown == 0)
			{
				/* Reset the countdown. */
				switch (psg->state->noise.frequency_mode)
				{
					case 0:
						psg->state->noise.countdown = 0x10;
						break;

					case 1:
						psg->state->noise.countdown = 0x20;
						break;

					case 2:
						psg->state->noise.countdown = 0x40;
						break;

					case 3:
						/* Use the last tone channel's frequency. */
						psg->state->noise.countdown = psg->state->tones[CC_COUNT_OF(psg->state->tones) - 1].countdown_master;
						break;
				}

				psg->state->noise.fake_output_bit = !psg->state->noise.fake_output_bit;

				if (psg->state->noise.fake_output_bit)
				{
					/* The noise channel works by maintaining a 16-bit register, whose bits are rotated every time
					   the output bit goes from low to high. The bit that was rotated from the 'bottom' of the
					   register to the 'top' is what is output to the speaker. In white noise mode, after rotation,
					   the bit at the 'top' is XOR'd with the bit that is third from the 'bottom'. */
					psg->state->noise.real_output_bit = (psg->state->noise.shift_register & 0x8000) >> 15;

					psg->state->noise.shift_register <<= 1;
					psg->state->noise.shift_register |= psg->state->noise.real_output_bit;

					if (psg->state->noise.type == PSG_NOISE_TYPE_WHITE)
						psg->state->noise.shift_register ^= (psg->state->noise.shift_register & 0x2000) >> 13;
				}
			}

			/* Output a sample. */
			*sample_buffer_pointer++ += psg_volumes[psg->state->noise.attenuation][psg->state->noise.real_output_bit];
		}
	}
}
