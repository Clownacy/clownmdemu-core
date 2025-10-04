#ifndef BUS_SUB_M68K_H
#define BUS_SUB_M68K_H

#include "bus-common.h"

/* TODO: Rename these to 'SubM68k'. */
void SyncMCDM68k(const ClownMDEmu *clownmdemu, CPUCallbackUserData *other_state, CycleMegaCD target_cycle);
cc_u16f MCDM68kReadCallbackWithCycle(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, CycleMegaCD target_cycle);
cc_u16f MCDM68kReadCallback(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u32f current_cycle);
void MCDM68kWriteCallbackWithCycle(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u16f value, CycleMegaCD target_cycle);
void MCDM68kWriteCallback(const void *user_data, cc_u32f address, cc_bool do_high_byte, cc_bool do_low_byte, cc_u32f current_cycle, cc_u16f value);

#endif /* BUS_SUB_M68K_H */
