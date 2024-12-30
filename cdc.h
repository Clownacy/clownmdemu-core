#ifndef CDC_H
#define CDC_H

#include "clowncommon/clowncommon.h"

#define CDC_SECTOR_SIZE 0x800

typedef const cc_u8l* (*CDC_SectorReadCallback)(void* user_data);

typedef struct CDC
{
	cc_u16l buffered_sectors[5][2 + CDC_SECTOR_SIZE / 2];

	cc_u32l current_sector, sectors_remaining;
	cc_u16l host_data_word_index;
	cc_u8l host_data_buffered_sector_index;
	cc_u8l buffered_sectors_read_index, buffered_sectors_write_index, buffered_sectors_total;
	cc_u8l device_destination;
	cc_bool host_data_target_sub_cpu, cdc_reading, host_data_bound;
} CDC;

void CDC_Initialise(CDC* cdc);
void CDC_Start(CDC* cdc, CDC_SectorReadCallback callback, const void *user_data);
void CDC_Stop(CDC* cdc);
cc_bool CDC_Stat(CDC* cdc, CDC_SectorReadCallback callback, const void *user_data);
cc_bool CDC_Read(CDC* cdc, CDC_SectorReadCallback callback, const void *user_data, cc_u32l *header);
cc_u16f CDC_HostData(CDC* cdc, cc_bool is_sub_cpu);
void CDC_Ack(CDC* cdc);
void CDC_Seek(CDC* cdc, CDC_SectorReadCallback callback, const void* user_data, cc_u32f sector, cc_u32f total_sectors);
cc_u16f CDC_Mode(CDC* cdc, cc_bool is_sub_cpu);
void CDC_SetDeviceDestination(CDC* cdc, cc_u16f device_destination);

#endif /* CDC_H */
