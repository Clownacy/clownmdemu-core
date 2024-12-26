cmake_minimum_required(VERSION 3.16.3)

option(CC_USE_C99_INTEGERS "Use C99 integer types instead of the original C89 ones. May save RAM depending on the platform's data model." OFF)

project(clownmdemu-core LANGUAGES C)

add_library(clownmdemu-core STATIC
	"bus-common.c"
	"bus-common.h"
	"bus-main-m68k.c"
	"bus-main-m68k.h"
	"bus-sub-m68k.c"
	"bus-sub-m68k.h"
	"bus-z80.c"
	"bus-z80.h"
	"cdc.c"
	"cdc.h"
	"clowncommon/clowncommon.h"
	"clownmdemu.c"
	"clownmdemu.h"
	"controller.c"
	"controller.h"
	"fm.c"
	"fm.h"
	"fm-channel.c"
	"fm-channel.h"
	"fm-operator.c"
	"fm-operator.h"
	"fm-phase.c"
	"fm-phase.h"
	"io-port.c"
	"io-port.h"
	"log.c"
	"log.h"
	"pcm.c"
	"pcm.h"
	"psg.c"
	"psg.h"
	"vdp.c"
	"vdp.h"
	"z80.c"
	"z80.h"
)

set_target_properties(clownmdemu-core PROPERTIES
	C_STANDARD 90
	C_STANDARD_REQUIRED NO
	C_EXTENSIONS OFF
)

add_subdirectory("clown68000" EXCLUDE_FROM_ALL)

target_link_libraries(clownmdemu-core PRIVATE clown68000-interpreter)

if(CC_USE_C99_INTEGERS)
	target_compile_definitions(clownmdemu-core PUBLIC CC_USE_C99_INTEGERS)
endif()
