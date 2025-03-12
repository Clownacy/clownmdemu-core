/*
TODO:

CSM Mode:
http://gendev.spritesmind.net/forum/viewtopic.php?p=5650#p5650

Differences between YM2612 and YM2608:
http://gendev.spritesmind.net/forum/viewtopic.php?p=5680#p5680

Timing of timer counter updates:
http://gendev.spritesmind.net/forum/viewtopic.php?p=5687#p5687

Test register that makes every channel output the DAC sample:
http://gendev.spritesmind.net/forum/viewtopic.php?t=1118

How the envelope generator works:
http://gendev.spritesmind.net/forum/viewtopic.php?p=5716#p5716
http://gendev.spritesmind.net/forum/viewtopic.php?p=6224#p6224
http://gendev.spritesmind.net/forum/viewtopic.php?p=6522#p6522

Sine table resolution, DAC and FM operator mixing/volume/clipping:
http://gendev.spritesmind.net/forum/viewtopic.php?p=5958#p5958

Maybe I should implement multiplexing instead of mixing the 6 channel together?
Multiplexing is more authentic, but is mixing *better*?

FM and PSG balancing:
http://gendev.spritesmind.net/forum/viewtopic.php?p=5960#p5960:

Operator output caching during algorithm stage:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6090#p6090

Self-feedback patents:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6096#p6096

How the operator unit works:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6114#p6114

How the phase generator works:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6177#p6177

Some details about the DAC:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6258#p6258

Operator unit value recycling:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6287#p6287
http://gendev.spritesmind.net/forum/viewtopic.php?p=6333#p6333

Some details on the internal tables:
http://gendev.spritesmind.net/forum/viewtopic.php?p=6741#p6741

LFO locking bug:
http://gendev.spritesmind.net/forum/viewtopic.php?p=7490#p7490

Multiplexing details:
http://gendev.spritesmind.net/forum/viewtopic.php?p=7509#p7509

Multiplexing ordering:
http://gendev.spritesmind.net/forum/viewtopic.php?p=7516#p7516

Corrected and expanded envelope generator stuff:
http://gendev.spritesmind.net/forum/viewtopic.php?p=7967#p7967

Some miscellaneous details (there's a bunch of minor stuff after it too):
http://gendev.spritesmind.net/forum/viewtopic.php?p=7999#p7999

YM3438 differences:
http://gendev.spritesmind.net/forum/viewtopic.php?p=8406#p8406

Phase generator steps:
http://gendev.spritesmind.net/forum/viewtopic.php?p=8908#p8908

Simulating the multiplex with addition:
http://gendev.spritesmind.net/forum/viewtopic.php?p=10441#p10441

DAC precision loss:
http://gendev.spritesmind.net/forum/viewtopic.php?p=11873#p11873

DAC and FM6 updating:
http://gendev.spritesmind.net/forum/viewtopic.php?p=14105#p14105

Some questions:
http://gendev.spritesmind.net/forum/viewtopic.php?p=18723#p18723

Debug register and die shot analysis:
http://gendev.spritesmind.net/forum/viewtopic.php?p=26964#p26964

The dumb busy flag:
http://gendev.spritesmind.net/forum/viewtopic.php?p=27124#p27124

Control unit (check stuff afterwards too):
http://gendev.spritesmind.net/forum/viewtopic.php?p=29471#p29471

More info about one of the test registers:
http://gendev.spritesmind.net/forum/viewtopic.php?p=30065#p30065

LFO queries:
http://gendev.spritesmind.net/forum/viewtopic.php?p=30935#p30935

More test register stuff:
http://gendev.spritesmind.net/forum/viewtopic.php?p=31285#p31285

And of course there's Nuked, which will answer all questions once and for all:
https://github.com/nukeykt/Nuked-OPN2

The 9th DAC sample bit.

*/

#include "fm.h"

#include <math.h>

#include "clowncommon/clowncommon.h"

#include "log.h"

static cc_u16f FM_ConvertTimerAValue(const cc_u16f value)
{
	return 0x400 - value;
}

static cc_u16f FM_ConvertTimerBValue(const cc_u16f value)
{
	return 0x10 * (0x100 - value);
}

void FM_Constant_Initialise(FM_Constant* const constant)
{
	FM_Channel_Constant_Initialise(&constant->channels);
}

void FM_State_Initialise(FM_State* const state)
{
	FM_ChannelMetadata *channel;
	cc_u8f i;

	for (channel = &state->channels[0]; channel < &state->channels[CC_COUNT_OF(state->channels)]; ++channel)
	{
		FM_Channel_State_Initialise(&channel->state);

		/* Panning must be enabled by default. Without this, Sonic 1's 'Sega' chant doesn't play. */
		channel->pan_left = cc_true;
		channel->pan_right = cc_true;
	}

	for (i = 0; i < CC_COUNT_OF(state->channel_3_metadata.frequencies); ++i)
		state->channel_3_metadata.frequencies[i] = 0;

	state->channel_3_metadata.per_operator_frequencies_enabled = cc_false;
	state->channel_3_metadata.csm_mode_enabled = cc_false;

	state->port = 0 * 3;
	state->address = 0;

	state->dac_sample = 0;
	state->dac_enabled = cc_false;

	state->raw_timer_a_value = 0;

	state->timers[0].value = FM_ConvertTimerAValue(0);
	state->timers[0].counter = FM_ConvertTimerAValue(0);
	state->timers[0].enabled = cc_false;

	state->timers[1].value = FM_ConvertTimerBValue(0);
	state->timers[1].counter = FM_ConvertTimerBValue(0);
	state->timers[1].enabled = cc_false;

	state->cached_address_27 = 0;
	state->cached_upper_frequency_bits = state->cached_upper_frequency_bits_fm3_multi_frequency = 0;
	state->leftover_cycles = 0;
	state->status = 0;
	state->busy_flag_counter = 0;

	FM_LFO_Initialise(&state->lfo);
}

void FM_Parameters_Initialise(FM* const fm, const FM_Configuration* const configuration, const FM_Constant* const constant, FM_State* const state)
{
	cc_u16f i;

	fm->configuration = configuration;
	fm->constant = constant;
	fm->state = state;

	for (i = 0; i < CC_COUNT_OF(fm->channels); ++i)
		FM_Channel_Parameters_Initialise(&fm->channels[i], &constant->channels, &state->channels[i].state);
}

void FM_DoAddress(const FM* const fm, const cc_u8f port, const cc_u8f address)
{
	fm->state->port = port * 3;
	fm->state->address = address;
}

void FM_DoData(const FM* const fm, const cc_u8f data)
{
	FM_State* const state = fm->state;

	/* Set BUSY flag. */
	state->status |= 0x80;
	/* The YM2612's BUSY flag is always active for exactly 32 internal cycles.
	   If I remember correctly, the YM3438 actually gives the BUSY flag
	   different durations based on the pending operation. */
	/* TODO: YM3438 BUSY flag durations. */
	state->busy_flag_counter = 32 * 6;

	if (state->address < 0x30)
	{
		if (state->port == 0)
		{
			switch (state->address)
			{
				default:
					LogMessage("Unrecognised FM address latched (0x%02" CC_PRIXFAST8 ")", state->address);
					break;

				case 0x22:
					state->lfo.enabled = (data & 8) != 0;
					state->lfo.frequency = data & 7;
					break;

				case 0x24:
					/* Oddly, the YM2608 manual describes these timers being twice as fast as they are here. */
					state->raw_timer_a_value &= 3;
					state->raw_timer_a_value |= data << 2;
					state->timers[0].value = FM_ConvertTimerAValue(state->raw_timer_a_value);
					break;

				case 0x25:
					state->raw_timer_a_value &= ~3;
					state->raw_timer_a_value |= data & 3;
					state->timers[0].value = FM_ConvertTimerAValue(state->raw_timer_a_value);
					break;

				case 0x26:
					state->timers[1].value = FM_ConvertTimerBValue(data);
					break;

				case 0x27:
				{
					const cc_bool fm3_per_operator_frequencies_enabled = (data & 0xC0) != 0;

					cc_u8f i;

					for (i = 0; i < CC_COUNT_OF(state->timers); ++i)
					{
						/* Only reload the timer on a rising edge. */
						if ((data & (1 << (0 + i))) != 0 && (state->cached_address_27 & (1 << (0 + i))) == 0)
							state->timers[i].counter = state->timers[i].value;

						/* Enable the timer. */
						state->timers[i].enabled = (data & (1 << (2 + i))) != 0;

						/* Clear the 'timer expired' flag. */
						if ((data & (1 << (4 + i))) != 0)
							state->status &= ~(1 << i);
					}

					/* Cache the contents of this write for the above rising-edge detection. */
					state->cached_address_27 = data;

					if (state->channel_3_metadata.per_operator_frequencies_enabled != fm3_per_operator_frequencies_enabled)
					{
						cc_u8f i;

						state->channel_3_metadata.per_operator_frequencies_enabled = fm3_per_operator_frequencies_enabled;

						for (i = 0; i < CC_COUNT_OF(fm->channels[2].operators); ++i)
							FM_Channel_SetFrequency(&fm->channels[2], i, state->channel_3_metadata.frequencies[fm3_per_operator_frequencies_enabled ? i : 3]);
					}

					state->channel_3_metadata.csm_mode_enabled = (data & 0xC0) == 0x80;

					break;
				}

				case 0x28:
				{
					/* Key on/off. */
					/* There's a gap between channels 3 and 4. */
					/* TODO: Check what happens if you try to access the 'gap' channels on real hardware. */
					static const cc_u8f table[8] = {0, 1, 2, 0, 3, 4, 5, 0};
					const cc_u8f table_index = data % CC_COUNT_OF(table);
					const FM_Channel* const channel = &fm->channels[table[table_index]];

					if (table_index == 3 || table_index == 7)
					{
						LogMessage("Key-on/off command uses invalid 'gap' channel index.");
						break;
					}

					/* TODO: Is this operator ordering actually correct? */
					FM_Channel_SetKeyOn(channel, 0, (data & (1 << 4)) != 0);
					FM_Channel_SetKeyOn(channel, 2, (data & (1 << 5)) != 0);
					FM_Channel_SetKeyOn(channel, 1, (data & (1 << 6)) != 0);
					FM_Channel_SetKeyOn(channel, 3, (data & (1 << 7)) != 0);

					break;
				}

				case 0x2A:
					/* DAC sample. */
					state->dac_sample &= 1u;
					state->dac_sample |= (cc_u16f)data << 1;
					break;

				case 0x2B:
					/* DAC enable/disable. */
					state->dac_enabled = (data & 0x80) != 0;
					break;

				case 0x2C:
					/* LST test 2 */
					state->dac_sample &= ~1u;
					state->dac_sample |= (data >> 3) & 1;
					break;
			}
		}
	}
	else
	{
		const cc_u16f slot_index = state->address & 3;
		const cc_u16f channel_index = state->port + slot_index;
		FM_ChannelMetadata* const channel_metadata = &state->channels[channel_index];
		const FM_Channel* const channel = &fm->channels[channel_index];

		/* There is no fourth channel per slot. */
		/* TODO: See how real hardware handles this. */
		if (slot_index == 3)
		{
			LogMessage("Attempted to access invalid fourth FM slot channel (address was 0x%02" CC_PRIXFAST8 ")", state->address);
		}
		else
		{
			if (state->address < 0xA0)
			{
				/* Per-operator. */
				const cc_u16f operator_index = (state->address >> 2) & 3;

				switch (state->address / 0x10)
				{
					default:
						LogMessage("Unrecognised FM address latched (0x%02" CC_PRIXFAST8 ")", state->address);
						break;

					case 0x30 / 0x10:
						/* Detune and multiplier. */
						FM_Channel_SetDetuneAndMultiplier(channel, operator_index, (data >> 4) & 7, data & 0xF);
						break;

					case 0x40 / 0x10:
						/* Total level. */
						FM_Channel_SetTotalLevel(channel, operator_index, data & 0x7F);
						break;

					case 0x50 / 0x10:
						/* Key scale and attack rate. */
						FM_Channel_SetKeyScaleAndAttackRate(channel, operator_index, (data >> 6) & 3, data & 0x1F);
						break;

					case 0x60 / 0x10:
						/* Amplitude modulation on and decay rate. */
						FM_Channel_SetDecayRate(channel, operator_index, data & 0x1F);
						FM_Channel_SetAmplitudeModulationOn(channel, operator_index, (data & 0x80) != 0);
						break;

					case 0x70 / 0x10:
						/* Sustain rate. */
						FM_Channel_SetSustainRate(channel, operator_index, data & 0x1F);
						break;

					case 0x80 / 0x10:
						/* Sustain level and release rate. */
						FM_Channel_SetSustainLevelAndReleaseRate(channel, operator_index, (data >> 4) & 0xF, data & 0xF);
						break;

					case 0x90 / 0x10:
						/* SSG-EG. */
						FM_Channel_SetSSGEG(channel, operator_index, data);
						break;
				}
			}
			else
			{
				/* Per-channel. */
				switch (state->address / 4)
				{
					default:
						LogMessage("Unrecognised FM address latched (0x%02" CC_PRIXFAST8 ")", state->address);
						break;

					case 0xA0 / 4:
					{
						/* Frequency low bits. */
						const cc_u16f frequency = data | (state->cached_upper_frequency_bits << 8);

						if (channel_index == 2) /* FM3 */
						{
							state->channel_3_metadata.frequencies[3] = frequency;

							if (state->channel_3_metadata.per_operator_frequencies_enabled)
							{
								FM_Channel_SetFrequency(&fm->channels[2], 3, frequency);
								break;
							}
						}

						FM_Channel_SetFrequencies(channel, frequency);
						break;
					}

					case 0xA4 / 4:
						/* Frequency high bits. */
						/* http://gendev.spritesmind.net/forum/viewtopic.php?p=5621#p5621 */
						state->cached_upper_frequency_bits = data & 0x3F;
						break;

					case 0xA8 / 4:
						/* Frequency low bits (multi-frequency). */
						if (state->port == 0)
						{
							static const cc_u8l operator_mappings[3] = {1, 0, 2}; /* Oddly, the operators are switched-around here, just like the accumulation logic. */ /* TODO: Look into this some more. */
							const cc_u8f operator_index = operator_mappings[slot_index];
							const cc_u16f frequency = data | (state->cached_upper_frequency_bits_fm3_multi_frequency << 8);

							state->channel_3_metadata.frequencies[operator_index] = frequency;

							if (state->channel_3_metadata.per_operator_frequencies_enabled)
								FM_Channel_SetFrequency(&fm->channels[2], operator_index, frequency);
						}

						break;

					case 0xAC / 4:
						/* Frequency high bits. */
						/* http://gendev.spritesmind.net/forum/viewtopic.php?p=5621#p5621 */
						state->cached_upper_frequency_bits_fm3_multi_frequency = data & 0x3F;
						break;

					case 0xB0 / 4:
						FM_Channel_SetFeedbackAndAlgorithm(channel, (data >> 3) & 7, data & 7);
						break;

					case 0xB4 / 4:
						/* Panning, AMS, FMS. */
						channel_metadata->pan_left = (data & 0x80) != 0;
						channel_metadata->pan_right = (data & 0x40) != 0;

						FM_Channel_SetModulationSensitivity(channel, (data >> 4) & 3, data & 7);
						break;
				}
			}
		}
	}
}

static cc_s16f GetFinalSample(const FM* const fm, cc_s16f sample, const cc_bool enabled)
{
	/* From 9-bit to 16-bit. */
	static const cc_s16f fm_volume_multiplier = (1L << 16) / (1 << 9);

	cc_s16f offset1, offset2;

	/* Approximate the 'ladder effect' bug. */
	/* Modelled after Nuked OPN2's implementation. */
	/* https://github.com/nukeykt/Nuked-OPN2/blob/335747d78cb0abbc3b55b004e62dad9763140115/ym3438.c#L987 */
	if (fm->configuration->ladder_effect_disabled)
	{
		offset1 = 0;
		offset2 = 0;
	}
	else if (sample < 0)
	{
		offset1 = 0;
		offset2 = -1;
	}
	else
	{
		offset1 = 1;
		offset2 = 1;
	}

	sample = (enabled ? sample + offset1 : offset2) + offset2 * 2;

	/* The FM sample is 9-bit, so convert it to 16-bit and then divide it so that it
	   can be mixed with the other five FM channels and the PSG without clipping. */
	return sample * fm_volume_multiplier / FM_VOLUME_DIVIDER;
}

void FM_OutputSamples(const FM* const fm, cc_s16l* const sample_buffer, const cc_u32f total_frames)
{
	FM_State* const state = fm->state;
	/* Convert from unsigned 9-bit PCM to signed 9-bit PCM. */
	const cc_s16f dac_sample = (cc_s16f)state->dac_sample - 0x100;

	const cc_s16l* const sample_buffer_end = &sample_buffer[total_frames * 2];

	cc_s16l *sample_buffer_pointer;

	for (sample_buffer_pointer = sample_buffer; sample_buffer_pointer != sample_buffer_end; sample_buffer_pointer += 2)
	{
		cc_u8f channel_index, timer_index;

		FM_LFO_Advance(&state->lfo);

		for (channel_index = 0; channel_index < CC_COUNT_OF(state->channels); ++channel_index)
		{
			const FM_ChannelMetadata* const channel_metadata = &state->channels[channel_index];
			const FM_Channel* const channel = &fm->channels[channel_index];
			const cc_bool pan_left = channel_metadata->pan_left;
			const cc_bool pan_right = channel_metadata->pan_right;

			const cc_bool is_dac = channel_index == 5 && state->dac_enabled;
			const cc_bool channel_disabled = is_dac ? fm->configuration->dac_channel_disabled : fm->configuration->fm_channels_disabled[channel_index];

			const cc_s16f fm_sample = FM_Channel_GetSample(channel, &state->lfo);
			const cc_s16f sample = is_dac ? dac_sample : fm_sample;

			if (!channel_disabled)
			{
				sample_buffer_pointer[0] += GetFinalSample(fm, sample, pan_left);
				sample_buffer_pointer[1] += GetFinalSample(fm, sample, pan_right);
			}
		}

		for (timer_index = 0; timer_index < CC_COUNT_OF(state->timers); ++timer_index)
		{
			FM_Timer* const timer = &state->timers[timer_index];

			if (--timer->counter == 0)
			{
				/* Set the 'timer expired' flag. */
				state->status |= timer->enabled ? 1 << timer_index : 0;

				/* Reload the timer's counter. */
				timer->counter = timer->value;

				/* Perform CSM key-on/key-off logic. */
				if (state->channel_3_metadata.csm_mode_enabled && timer_index == 0)
				{
					cc_u8f operator_index;

					for (operator_index = 0; operator_index < CC_COUNT_OF(fm->channels[2].operators); ++operator_index)
					{
						FM_Channel_SetKeyOn(&fm->channels[2], operator_index, cc_true);
						FM_Channel_SetKeyOn(&fm->channels[2], operator_index, cc_false);
					}
				}
			}
		}
	}
}

cc_u8f FM_Update(const FM* const fm, const cc_u32f cycles_to_do, void (* const fm_audio_to_be_generated)(const void *user_data, cc_u32f total_frames), const void* const user_data)
{
	FM_State* const state = fm->state;
	const cc_u32f total_frames = (state->leftover_cycles + cycles_to_do) / FM_SAMPLE_RATE_DIVIDER;

	state->leftover_cycles = (state->leftover_cycles + cycles_to_do) % FM_SAMPLE_RATE_DIVIDER;

	if (total_frames != 0)
		fm_audio_to_be_generated(user_data, total_frames);

	/* Decrement the BUSY flag counter. */
	if (state->busy_flag_counter != 0)
	{
		state->busy_flag_counter -= CC_MIN(state->busy_flag_counter, cycles_to_do);

		/* Clear BUSY flag if the counter has elapsed. */
		if (state->busy_flag_counter == 0)
			state->status &= ~0x80;
	}

	return state->status;
}
