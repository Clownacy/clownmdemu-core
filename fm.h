#ifndef FM_H
#define FM_H

#include <stddef.h>

#include "clowncommon/clowncommon.h"

#include "fm-channel.h"
#include "fm-lfo.h"

/* 8 is chosen because there are 6 FM channels (of which the DAC can replace one).
   Dividing by 8 is simpler than dividing by 6, so that was opted for instead. */
#define FM_VOLUME_DIVIDER 8

#define FM_PRESCALER 6

/* 6 for the number of channels, and 4 for the number of operators per channel. */
#define FM_SAMPLE_RATE_DIVIDER (FM_PRESCALER * 6 * 4)

#define FM_PARAMETERS_INITIALISE(CONFIGURATION, CONSTANT, STATE) { \
		(CONFIGURATION), \
		(CONSTANT), \
		(STATE), \
\
		{ \
			FM_CHANNEL_PARAMETERS_INITIALISE( \
				&(CONSTANT)->channels, \
				&(STATE)->channels[0].state \
			), \
\
			FM_CHANNEL_PARAMETERS_INITIALISE( \
				&(CONSTANT)->channels, \
				&(STATE)->channels[1].state \
			), \
\
			FM_CHANNEL_PARAMETERS_INITIALISE( \
				&(CONSTANT)->channels, \
				&(STATE)->channels[2].state \
			), \
\
			FM_CHANNEL_PARAMETERS_INITIALISE( \
				&(CONSTANT)->channels, \
				&(STATE)->channels[3].state \
			), \
\
			FM_CHANNEL_PARAMETERS_INITIALISE( \
				&(CONSTANT)->channels, \
				&(STATE)->channels[4].state \
			), \
\
			FM_CHANNEL_PARAMETERS_INITIALISE( \
				&(CONSTANT)->channels, \
				&(STATE)->channels[5].state \
			) \
		} \
	}

typedef struct FM_ChannelMetadata
{
	FM_Channel_State state;
	cc_bool pan_left;
	cc_bool pan_right;
} FM_ChannelMetadata;

typedef struct FM_Configuration
{
	cc_bool fm_channels_disabled[6];
	cc_bool dac_channel_disabled;
	cc_bool ladder_effect_disabled;
} FM_Configuration;

typedef struct FM_Constant
{
	FM_Channel_Constant channels;
} FM_Constant;

typedef struct FM_Timer
{
	cc_u32l value;
	cc_u32l counter;
	cc_bool enabled;
} FM_Timer;

typedef struct FM_State
{
	FM_ChannelMetadata channels[6];
	struct
	{
		cc_u16l frequencies[4];
		cc_bool per_operator_frequencies_enabled;
		cc_bool csm_mode_enabled;
	} channel_3_metadata;
	cc_u8l port;
	cc_u8l address;
	cc_u16l dac_sample;
	cc_bool dac_enabled, dac_test;
	cc_u16l raw_timer_a_value;
	FM_Timer timers[2];
	cc_u8l cached_address_27;
	cc_u8l cached_upper_frequency_bits, cached_upper_frequency_bits_fm3_multi_frequency;
	cc_u8l leftover_cycles;
	cc_u8l status;
	cc_u8l busy_flag_counter;
	FM_LFO lfo;
} FM_State;

typedef struct FM
{
	const FM_Configuration *configuration;
	const FM_Constant *constant;
	FM_State *state;

	FM_Channel channels[6];
} FM;

void FM_Constant_Initialise(FM_Constant *constant);
void FM_State_Initialise(FM_State *state);
void FM_Parameters_Initialise(FM *fm, const FM_Configuration *configuration, const FM_Constant *constant, FM_State *state);

void FM_DoAddress(const FM *fm, cc_u8f port, cc_u8f address);
void FM_DoData(const FM *fm, cc_u8f data);

void FM_OutputSamples(const FM *fm, cc_s16l *sample_buffer, cc_u32f total_frames);
/* Updates the FM's internal state and outputs samples. */
/* The samples are stereo and in signed 16-bit PCM format. */
cc_u8f FM_Update(const FM *fm, cc_u32f cycles_to_do, void (*fm_audio_to_be_generated)(const void *user_data, cc_u32f total_frames), const void *user_data);

#endif /* FM_H */
