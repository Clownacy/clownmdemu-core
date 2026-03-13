#ifndef BUS_Z80_H
#define BUS_Z80_H

#include "bus-common.h"

void SyncZ80(ClownMDEmu *clownmdemu, CPUCallbackUserData *other_state, CycleMegaDrive target_cycle);
cc_u16f Z80ReadCallbackWithCycle(const void *user_data, cc_u16f address, CycleMegaDrive target_cycle);
cc_u16f Z80ReadCallback(void *user_data, cc_u16f address);
void Z80WriteCallbackWithCycle(const void *user_data, cc_u16f address, cc_u16f value, CycleMegaDrive target_cycle);
void Z80WriteCallback(void *user_data, cc_u16f address, cc_u16f value);

#endif /* BUS_Z80_H */
