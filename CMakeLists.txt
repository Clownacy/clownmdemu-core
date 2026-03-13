cmake_minimum_required(VERSION 3.0...3.16.3)

option(CC_USE_C99_INTEGERS "Use C99 integer types instead of the original C89 ones. May save RAM depending on the platform's data model." OFF)

project(clownmdemu-core LANGUAGES C)

add_library(clownmdemu-core STATIC
	"libraries/clowncommon/clowncommon.h"
	"source/bus-common.c"
	"source/bus-common.h"
	"source/bus-main-m68k.c"
	"source/bus-main-m68k.h"
	"source/bus-sub-m68k.c"
	"source/bus-sub-m68k.h"
	"source/bus-z80.c"
	"source/bus-z80.h"
	"source/cdc.c"
	"source/cdc.h"
	"source/cdda.c"
	"source/cdda.h"
	"source/clownmdemu.c"
	"source/clownmdemu.h"
	"source/controller.c"
	"source/controller.h"
	"source/controller-manager.c"
	"source/controller-manager.h"
	"source/controller-multitap-ea.c"
	"source/controller-multitap-ea.h"
	"source/controller-multitap-sega.c"
	"source/controller-multitap-sega.h"
	"source/fm.c"
	"source/fm.h"
	"source/fm-channel.c"
	"source/fm-channel.h"
	"source/fm-lfo.c"
	"source/fm-lfo.h"
	"source/fm-operator.c"
	"source/fm-operator.h"
	"source/fm-phase.c"
	"source/fm-phase.h"
	"source/io-port.c"
	"source/io-port.h"
	"source/log.c"
	"source/log.h"
	"source/low-pass-filter.c"
	"source/low-pass-filter.h"
	"source/pcm.c"
	"source/pcm.h"
	"source/psg.c"
	"source/psg.h"
	"source/vdp.c"
	"source/vdp.h"
)

add_subdirectory("libraries/clown68000" EXCLUDE_FROM_ALL)
target_link_libraries(clownmdemu-core PRIVATE clown68000-interpreter)

add_subdirectory("libraries/clownz80" EXCLUDE_FROM_ALL)
target_link_libraries(clownmdemu-core PRIVATE clownz80-interpreter)

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clownmdemu-core PUBLIC CC_USE_C99_INTEGERS)
endif()
