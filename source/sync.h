#ifndef SYNC_H
#define SYNC_H

#include "../libraries/clowncommon/clowncommon.h"

typedef struct Sync_State
{
	cc_s32l cycles_available;
} Sync_State;

typedef struct Sync_Temporary
{
	cc_u32l current_cycle;
} Sync_Temporary;

typedef cc_u32f (*Sync_Callback)(void *user_data, cc_u32f cycles_to_do);

void Sync_State_Initialise(Sync_State *state);
void Sync_Temporary_Initialise(Sync_Temporary *temporary);
void Sync_Update(Sync_State *state, Sync_Temporary *temporary, cc_u32f current_cycle, Sync_Callback callback, const void *user_data);

#endif /* SYNC_H */
