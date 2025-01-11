#ifndef CLOWNMDEMU_H
#define CLOWNMDEMU_H

#include <stdarg.h>
#include <stddef.h>

#include "clowncommon/clowncommon.h"

#include "cdc.h"
#include "cdda.h"
#include "clown68000/interpreter/clown68000.h"
#include "controller.h"
#include "fm.h"
#include "io-port.h"
#include "pcm.h"
#include "psg.h"
#include "vdp.h"
#include "z80.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TODO - Documentation */

#define CLOWNMDEMU_PARAMETERS_INITIALISE(CONFIGURATION, CONSTANT, STATE, CALLBACKS) { \
		(CONFIGURATION), \
		(CONSTANT), \
		(STATE), \
		(CALLBACKS), \
\
		&(STATE)->m68k.state, \
\
		{ \
			&(CONSTANT)->z80, \
			&(STATE)->z80.state \
		}, \
\
		&(STATE)->mega_cd.m68k.state, \
\
		{ \
			&(CONFIGURATION)->vdp, \
			&(CONSTANT)->vdp, \
			&(STATE)->vdp \
		}, \
\
		FM_PARAMETERS_INITIALISE( \
			&(CONFIGURATION)->fm, \
			&(CONSTANT)->fm, \
			&(STATE)->fm \
		), \
\
		{ \
			&(CONFIGURATION)->psg, \
			&(CONSTANT)->psg, \
			&(STATE)->psg \
		}, \
\
		{ \
			&(CONFIGURATION)->pcm, \
			&(STATE)->mega_cd.pcm \
		} \
	}

/* Mega Drive */
#define CLOWNMDEMU_MASTER_CLOCK_NTSC 53693175
#define CLOWNMDEMU_MASTER_CLOCK_PAL  53203424

#define CLOWNMDEMU_M68K_CLOCK_DIVIDER 7
#define CLOWNMDEMU_M68K_CLOCK_NTSC (CLOWNMDEMU_MASTER_CLOCK_NTSC / CLOWNMDEMU_M68K_CLOCK_DIVIDER)
#define CLOWNMDEMU_M68K_CLOCK_PAL  (CLOWNMDEMU_MASTER_CLOCK_PAL / CLOWNMDEMU_M68K_CLOCK_DIVIDER)

#define CLOWNMDEMU_Z80_CLOCK_DIVIDER 15
#define CLOWNMDEMU_Z80_CLOCK_NTSC (CLOWNMDEMU_MASTER_CLOCK_NTSC / CLOWNMDEMU_Z80_CLOCK_DIVIDER)
#define CLOWNMDEMU_Z80_CLOCK_PAL  (CLOWNMDEMU_MASTER_CLOCK_PAL / CLOWNMDEMU_Z80_CLOCK_DIVIDER)

#define CLOWNMDEMU_FM_SAMPLE_RATE_NTSC (CLOWNMDEMU_M68K_CLOCK_NTSC / FM_SAMPLE_RATE_DIVIDER)
#define CLOWNMDEMU_FM_SAMPLE_RATE_PAL  (CLOWNMDEMU_M68K_CLOCK_PAL / FM_SAMPLE_RATE_DIVIDER)

#define CLOWNMDEMU_PSG_SAMPLE_RATE_DIVIDER 16
#define CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC (CLOWNMDEMU_Z80_CLOCK_NTSC / CLOWNMDEMU_PSG_SAMPLE_RATE_DIVIDER)
#define CLOWNMDEMU_PSG_SAMPLE_RATE_PAL  (CLOWNMDEMU_Z80_CLOCK_PAL / CLOWNMDEMU_PSG_SAMPLE_RATE_DIVIDER)

/* Mega CD */
#define CLOWNMDEMU_MCD_MASTER_CLOCK 50000000
#define CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER 4
#define CLOWNMDEMU_MCD_M68K_CLOCK (CLOWNMDEMU_MCD_MASTER_CLOCK / CLOWNMDEMU_MCD_M68K_CLOCK_DIVIDER)

#define CLOWNMDEMU_PCM_SAMPLE_RATE_DIVIDER 0x180
#define CLOWNMDEMU_PCM_SAMPLE_RATE (CLOWNMDEMU_MCD_M68K_CLOCK / CLOWNMDEMU_PCM_SAMPLE_RATE_DIVIDER)

/* The NTSC framerate is 59.94FPS (60 divided by 1.001) */
#define CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(x) ((x) * (60 * 1000) / 1001)
#define CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(x) (((x) / 60) + ((x) / (60 * 1000)))

/* The PAL framerate is 50FPS */
#define CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(x) ((x) * 50)
#define CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(x) ((x) / 50)

typedef enum ClownMDEmu_Button
{
	CLOWNMDEMU_BUTTON_UP,
	CLOWNMDEMU_BUTTON_DOWN,
	CLOWNMDEMU_BUTTON_LEFT,
	CLOWNMDEMU_BUTTON_RIGHT,
	CLOWNMDEMU_BUTTON_A,
	CLOWNMDEMU_BUTTON_B,
	CLOWNMDEMU_BUTTON_C,
	CLOWNMDEMU_BUTTON_X,
	CLOWNMDEMU_BUTTON_Y,
	CLOWNMDEMU_BUTTON_Z,
	CLOWNMDEMU_BUTTON_START,
	CLOWNMDEMU_BUTTON_MODE,
	CLOWNMDEMU_BUTTON_MAX
} ClownMDEmu_Button;

typedef enum ClownMDEmu_Region
{
	CLOWNMDEMU_REGION_DOMESTIC, /* Japanese */
	CLOWNMDEMU_REGION_OVERSEAS  /* Elsewhere */
} ClownMDEmu_Region;

typedef enum ClownMDEmu_TVStandard
{
	CLOWNMDEMU_TV_STANDARD_NTSC, /* 60Hz */
	CLOWNMDEMU_TV_STANDARD_PAL   /* 50Hz */
} ClownMDEmu_TVStandard;

typedef enum ClownMDEmu_CDDAMode
{
	CLOWNMDEMU_CDDA_PLAY_ALL,
	CLOWNMDEMU_CDDA_PLAY_ONCE,
	CLOWNMDEMU_CDDA_PLAY_REPEAT
} ClownMDEmu_CDDAMode;

typedef struct ClownMDEmu_Configuration
{
	struct
	{
		ClownMDEmu_Region region;
		ClownMDEmu_TVStandard tv_standard;
	} general;

	VDP_Configuration vdp;
	FM_Configuration fm;
	PSG_Configuration psg;
	PCM_Configuration pcm;
} ClownMDEmu_Configuration;

typedef struct ClownMDEmu_Constant
{
	Z80_Constant z80;
	VDP_Constant vdp;
	FM_Constant fm;
	PSG_Constant psg;
} ClownMDEmu_Constant;

typedef struct ClownMDEmu_State
{
	struct
	{
		Clown68000_State state;
		cc_u16l ram[0x8000];
		cc_u32l cycle_countdown;
	} m68k;

	struct
	{
		Z80_State state;
		cc_u8l ram[0x2000];
		cc_u32l cycle_countdown;
		cc_u16l bank;
		cc_bool bus_requested;
		cc_bool reset_held;
	} z80;

	VDP_State vdp;
	FM_State fm;
	PSG_State psg;
	IOPort io_ports[3];
	Controller controllers[2];

	struct
	{
		cc_u8l buffer[0x4000]; /* This is the size required by Phantasy Star 4. */
		cc_u16l size;
		cc_bool non_volatile;
		cc_u8l data_size;
		cc_u8l device_type;
		cc_bool mapped_in;
	} external_ram;

	cc_u16l current_scanline;

	struct
	{
		struct
		{
			Clown68000_State state;
			cc_u32l cycle_countdown;
			cc_bool bus_requested;
			cc_bool reset_held;
		} m68k;

		struct
		{
			cc_u16l buffer[0x40000];
			cc_u8l bank;
			cc_u8l write_protect;
		} prg_ram;

		struct
		{
			cc_u16l buffer[0x20000];
			cc_bool in_1m_mode;
			cc_bool dmna, ret;
		} word_ram;

		struct
		{
			cc_u16l flag;
			cc_u16l command[8]; /* The MAIN-CPU one. */
			cc_u16l status[8];  /* The SUB-CPU one. */
		} communication;

		struct
		{
			cc_bool enabled[6];
			cc_bool irq1_pending;
			cc_u32l irq3_countdown, irq3_countdown_master;
		} irq;

		/* TODO: Just convert this to a plain array? Presumably, that's what the original hardware does. */
		struct
		{
			cc_bool large_stamp_map, large_stamp, repeating_stamp_map;
			cc_u16l stamp_map_address, image_buffer_address, image_buffer_width;
			cc_u8l image_buffer_height, image_buffer_height_in_tiles, image_buffer_x_offset, image_buffer_y_offset;
		} rotation;

		CDC cdc;
		CDDA cdda;
		PCM_State pcm;

		cc_bool boot_from_cd;
		cc_u16l hblank_address;
		cc_u16l delayed_dma_word;
	} mega_cd;
} ClownMDEmu_State;

struct ClownMDEmu;

typedef struct ClownMDEmu_Callbacks
{
	const void *user_data;

	/* TODO: Rename these to be less mind-numbing. */
	cc_u8f (*cartridge_read)(void *user_data, cc_u32f address);
	void (*cartridge_written)(void *user_data, cc_u32f address, cc_u8f value);
	void (*colour_updated)(void *user_data, cc_u16f index, cc_u16f colour);
	void (*scanline_rendered)(void *user_data, cc_u16f scanline, const cc_u8l *pixels, cc_u16f screen_width, cc_u16f screen_height);
	cc_bool (*input_requested)(void *user_data, cc_u8f player_id, ClownMDEmu_Button button_id);
	void (*fm_audio_to_be_generated)(void *user_data, const struct ClownMDEmu *clownmdemu, size_t total_frames, void (*generate_fm_audio)(const struct ClownMDEmu *clownmdemu, cc_s16l *sample_buffer, size_t total_frames));
	void (*psg_audio_to_be_generated)(void *user_data, const struct ClownMDEmu *clownmdemu, size_t total_frames, void (*generate_psg_audio)(const struct ClownMDEmu *clownmdemu, cc_s16l *sample_buffer, size_t total_frames));
	void (*pcm_audio_to_be_generated)(void *user_data, const struct ClownMDEmu *clownmdemu, size_t total_frames, void (*generate_pcm_audio)(const struct ClownMDEmu *clownmdemu, cc_s16l *sample_buffer, size_t total_frames));
	void (*cdda_audio_to_be_generated)(void *user_data, const struct ClownMDEmu *clownmdemu, size_t total_frames, void (*generate_cdda_audio)(const struct ClownMDEmu *clownmdemu, cc_s16l *sample_buffer, size_t total_frames));
	void (*cd_seeked)(void *user_data, cc_u32f sector_index);
	CDC_SectorReadCallback cd_sector_read;
	cc_bool (*cd_track_seeked)(void *user_data, cc_u16f track_index, ClownMDEmu_CDDAMode mode);
	CDDA_AudioReadCallback cd_audio_read;
} ClownMDEmu_Callbacks;

typedef struct ClownMDEmu
{
	const ClownMDEmu_Configuration *configuration;
	const ClownMDEmu_Constant *constant;
	ClownMDEmu_State *state;
	const ClownMDEmu_Callbacks *callbacks;

	Clown68000_State *m68k;
	Z80 z80;
	Clown68000_State *mcd_m68k;
	VDP vdp;
	FM fm;
	PSG psg;
	PCM pcm;
} ClownMDEmu;

typedef void (*ClownMDEmu_LogCallback)(void *user_data, const char *format, va_list arg);

void ClownMDEmu_Constant_Initialise(ClownMDEmu_Constant *constant);
void ClownMDEmu_State_Initialise(ClownMDEmu_State *state);
void ClownMDEmu_Parameters_Initialise(ClownMDEmu *clownmdemu, const ClownMDEmu_Configuration *configuration, const ClownMDEmu_Constant *constant, ClownMDEmu_State *state, const ClownMDEmu_Callbacks *callbacks);
void ClownMDEmu_Iterate(const ClownMDEmu *clownmdemu);
void ClownMDEmu_Reset(const ClownMDEmu *clownmdemu, const cc_bool cd_boot);
void ClownMDEmu_SetLogCallback(const ClownMDEmu_LogCallback log_callback, const void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* CLOWNMDEMU_H */
