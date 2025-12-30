#ifndef BUS_MAIN_M68K_H
#define BUS_MAIN_M68K_H

#include "bus-common.h"

void SyncM68k(ClownMDEmu *clownmdemu, CPUCallbackUserData *other_state, CycleMegaDrive target_cycle);
cc_u16f M68kReadCallbackWithCycleWithDMA(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, CycleMegaDrive target_cycle, cc_bool is_vdp_dma);
cc_u16f M68kReadCallbackWithCycle(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, CycleMegaDrive target_cycle);
cc_u16f M68kReadCallback(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u32f current_cycle);
void M68kWriteCallbackWithCycle(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u16f value, CycleMegaDrive target_cycle);
void M68kWriteCallback(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u32f current_cycle, cc_u16f value);

#endif /* BUS_MAIN_M68K_H */
