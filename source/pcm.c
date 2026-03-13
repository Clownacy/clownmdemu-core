#include "pcm.h"

#include <string.h>

/* MEGA-CD HARDWARE MANUAL - PCM SOUND SOURCE */
/* https://segaretro.org/images/2/2d/MCDHardware_Manual_PCM_Sound_Source.pdf */

/* RF5C68A manual */
/* https://segaretro.org/images/2/22/RF5C68A.pdf */

void PCM_Initialise(PCM* const pcm)
{
	cc_u8f i;

	for (i = 0; i < CC_COUNT_OF(pcm->state.channels); ++i)
	{
		PCM_ChannelState* const channel = &pcm->state.channels[i];

		cc_u8f j;

		channel->disabled = cc_true;
		channel->volume = 0;
		for (j = 0; j < CC_COUNT_OF(channel->panning); ++j)
			channel->panning[j] = 0;
		channel->frequency = 0;
		channel->loop_address = 0;
		channel->start_address = 0;
		channel->address = 0;
	}

	pcm->state.sounding = cc_false;
	pcm->state.current_wave_bank = 0;
	pcm->state.current_channel = 0;

	memset(pcm->state.wave_ram, 0, sizeof(pcm->state.wave_ram));
}

static cc_bool PCM_IsChannelAudible(const PCM* const pcm, const PCM_ChannelState* const channel)
{
	return !channel->disabled && pcm->state.sounding;
}

static void PCM_CheckChannelResets(PCM* const pcm)
{
	size_t i;

	for (i = 0; i < CC_COUNT_OF(pcm->state.channels); ++i)
	{
		PCM_ChannelState* const channel = &pcm->state.channels[i];

		if (!PCM_IsChannelAudible(pcm, channel))
			channel->address = (cc_u32f)channel->start_address << 19;
	}
}

void PCM_WriteRegister(PCM* const pcm, const cc_u16f reg, const cc_u8f value)
{
	PCM_ChannelState* const current_channel = &pcm->state.channels[pcm->state.current_channel];

	switch (reg)
	{
		case 0:
			current_channel->volume = value;
			break;
			
		case 1:
			current_channel->panning[0] = value & 0xF;
			current_channel->panning[1] = value >> 4;
			break;
			
		case 2:
			current_channel->frequency &= 0xFF00;
			current_channel->frequency |= value;
			break;
			
		case 3:
			current_channel->frequency &= 0x00FF;
			current_channel->frequency |= (cc_u16f)value << 8;
			break;
			
		case 4:
			current_channel->loop_address &= 0xFF00;
			current_channel->loop_address |= value;
			break;
			
		case 5:
			current_channel->loop_address &= 0x00FF;
			current_channel->loop_address |= (cc_u16f)value << 8;
			break;

		case 6:
			current_channel->start_address = value;
			if (!PCM_IsChannelAudible(pcm, current_channel))
				current_channel->address = (cc_u32f)current_channel->start_address << 19;
			break;

		case 7:
			pcm->state.sounding = (value & 0x80) != 0;
			PCM_CheckChannelResets(pcm);

			if ((value & 0x40) != 0)
				pcm->state.current_channel = value & 7;
			else
				pcm->state.current_wave_bank = value & 0xF;

			break;

		case 8:
		{
			size_t i;

			for (i = 0; i < CC_COUNT_OF(pcm->state.channels); ++i)
				pcm->state.channels[i].disabled = ((value >> i) & 1) != 0;
			PCM_CheckChannelResets(pcm);

			break;
		}
	}
}

cc_u8f PCM_ReadRegister(const PCM* const pcm, const cc_u16f reg)
{
	const PCM_ChannelState* const current_channel = &pcm->state.channels[pcm->state.current_channel];

	cc_u8f value = 0;

	switch (reg)
	{
		case 0x00:
			value = current_channel->volume;
			break;
			
		case 0x01:
			value = (current_channel->panning[0] << 0) | (current_channel->panning[1] << 4);
			break;
			
		case 0x02:
			value = (current_channel->frequency >> 0) & 0xFF;
			break;
			
		case 0x03:
			value = (current_channel->frequency >> 8) & 0xFF;
			break;
			
		case 0x04:
			value = (current_channel->loop_address >> 0) & 0xFF;
			break;
			
		case 0x05:
			value = (current_channel->loop_address >> 8) & 0xFF;
			break;

		case 0x06:
			value = current_channel->start_address;
			break;

		case 0x08:
		{
			size_t i;

			for (i = 0; i < CC_COUNT_OF(pcm->state.channels); ++i)
				value |= pcm->state.channels[i].disabled << i;

			break;
		}

		case 0x10:
		case 0x12:
		case 0x14:
		case 0x16:
		case 0x18:
		case 0x1A:
		case 0x1C:
		case 0x1E:
			value = (pcm->state.channels[(reg - 0x10) / 2].address >> 11) & 0xFF;
			break;

		case 0x11:
		case 0x13:
		case 0x15:
		case 0x17:
		case 0x19:
		case 0x1B:
		case 0x1D:
		case 0x1F:
			value = (pcm->state.channels[(reg - 0x11) / 2].address >> 19) & 0xFF;
			break;
	}

	return value;
}

cc_u8f PCM_ReadWaveRAM(const PCM* const pcm, const cc_u16f address)
{
	return pcm->state.wave_ram[(pcm->state.current_wave_bank << 12) + (address & 0xFFF)];
}

void PCM_WriteWaveRAM(PCM* const pcm, const cc_u16f address, const cc_u8f value)
{
	pcm->state.wave_ram[(pcm->state.current_wave_bank << 12) + (address & 0xFFF)] = value;
}

static cc_u8f PCM_FetchSample(const PCM* const pcm, const PCM_ChannelState* const channel)
{
	return pcm->state.wave_ram[(channel->address >> 11) & 0xFFFF];
}

static cc_u8f PCM_UpdateAddressAndFetchSample(const PCM* const pcm, PCM_ChannelState* const channel)
{
	cc_u8f wave_value = 0;

	if (PCM_IsChannelAudible(pcm, channel))
	{
		/* Read sample and advance address. */
		wave_value = PCM_FetchSample(pcm, channel);
		channel->address += channel->frequency;
		channel->address &= 0x7FFFFFF;

		/* Handle looping. */
		if (wave_value == 0xFF)
		{
			channel->address = channel->loop_address << 11;
			wave_value = PCM_FetchSample(pcm, channel);
		}
	}

	return wave_value;
}

static cc_s16f PCM_UnsignedToSigned(const cc_u16f sample)
{
	cc_s16f signed_sample;

	/* Samples can be -0x8000, but that is incompatible with non-two's-complement 16-bit integers, which this emulator supports. */
	if (sample == 0)
		signed_sample = -0x7FFF;
	else if ((sample & 0x8000) != 0)
		signed_sample = (cc_s16f)(sample - 0x8000);
	else
		signed_sample = -(cc_s16f)(0x8000 - sample);

	return signed_sample;
}

void PCM_Update(PCM* const pcm, cc_s16l* const sample_buffer, const size_t total_frames)
{
	cc_s16l *sample_pointer = sample_buffer;
	size_t current_frame;

	for (current_frame = 0; current_frame < total_frames; ++current_frame)
	{
		cc_u16f mixed_samples[2] = {0x8000, 0x8000};
		cc_u8f current_channel;
		cc_u8f current_mixed_sample;

		for (current_channel = 0; current_channel < CC_COUNT_OF(pcm->state.channels); ++current_channel)
		{
			PCM_ChannelState* const channel = &pcm->state.channels[current_channel];

			const cc_u8f sample = PCM_UpdateAddressAndFetchSample(pcm, channel);

			if (PCM_IsChannelAudible(pcm, channel) && !pcm->configuration.channels_disabled[current_channel])
			{
				for (current_mixed_sample = 0; current_mixed_sample < CC_COUNT_OF(mixed_samples); ++current_mixed_sample)
				{
					/* Mask out direction bit and apply volume and panning. */
					const cc_u8f absolute_sample = sample & 0x7F;
					const cc_bool add_bit = (sample & 0x80) != 0;
					const cc_u32f scaled_absolute_sample = ((cc_u32f)absolute_sample * channel->volume * channel->panning[current_mixed_sample]) >> 5;

					cc_u32f mixed_sample = mixed_samples[current_mixed_sample];

					/* TODO: Check if this is how real hardware handles clipping, or if it's only done after mixing. */
					if (add_bit)
					{
						mixed_sample += scaled_absolute_sample;

						/* Handle overflow. */
						if (mixed_sample > 0xFFFF)
							mixed_sample = 0xFFFF;
					}
					else
					{
						mixed_sample -= scaled_absolute_sample;

						/* Handle underflow. */
						if (mixed_sample > 0xFFFF)
							mixed_sample = 0x0000;
					}

					mixed_samples[current_mixed_sample] = mixed_sample;
				}
			}
		}

		for (current_mixed_sample = 0; current_mixed_sample < CC_COUNT_OF(mixed_samples); ++current_mixed_sample)
		{
			/* TODO: Just set, rather than add, and make the mixer reflect this too. */
			*sample_pointer++ += PCM_UnsignedToSigned(mixed_samples[current_mixed_sample]);
		}
	}
}
