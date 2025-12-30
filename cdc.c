#include "cdc.h"

#include "log.h"

#define CDC_END(CDC) CC_COUNT_OF((CDC)->buffered_sectors[0])

static cc_u8f To2DigitBCD(const cc_u8f value)
{
	const cc_u8f lower_digit = value % 10;
	const cc_u8f upper_digit = (value / 10) % 10;
	return (upper_digit << 4) | (lower_digit << 0);
}

static void GetCDSectorHeaderBytes(const CDC_State* const state, cc_u8l* const buffer)
{
	buffer[0] = To2DigitBCD(state->current_sector / (75 * 60));
	buffer[1] = To2DigitBCD((state->current_sector / 75) % 60);
	buffer[2] = To2DigitBCD(state->current_sector % 75);
	/* TODO: Is this byte correct? */
	buffer[3] = 0x01;
}

/* TODO: De-duplicate this 'bytes to integer' logic. */
static cc_u16f BytesToU16(const cc_u8l* const bytes)
{
	return (cc_u16f)bytes[0] << 8 | bytes[1];
}

static cc_u32f U16sToU32(const cc_u16l* const u16s)
{
	return (cc_u32f)u16s[0] << 16 | u16s[1];
}

static cc_bool EndOfDataTransfer(CDC_State* const state)
{
	return state->host_data_word_index >= CDC_END(state) - 1;
}

static cc_bool DataSetReady(CDC_State* const state)
{
	return state->host_data_word_index != CDC_END(state);
}

static void RefillSectorBuffer(CDC_State* const state, const CDC_SectorReadCallback cd_sector_read, const void* const user_data)
{
	if (!state->cdc_reading)
		return;

	/* TODO: Stop reading sectors instantaneously! */
	while (state->buffered_sectors_total != CC_COUNT_OF(state->buffered_sectors))
	{
		cc_u8l header_bytes[4];
		cc_u16l* const sector_words = state->buffered_sectors[state->buffered_sectors_write_index];

		GetCDSectorHeaderBytes(state, header_bytes);

		sector_words[0] = BytesToU16(&header_bytes[0]);
		sector_words[1] = BytesToU16(&header_bytes[2]);
		cd_sector_read((void*)user_data, &sector_words[2]);

		++state->current_sector;

		++state->buffered_sectors_total;
		++state->buffered_sectors_write_index;

		if (state->buffered_sectors_write_index == CC_COUNT_OF(state->buffered_sectors))
			state->buffered_sectors_write_index = 0;

		if (state->sectors_remaining != 0 && --state->sectors_remaining == 0)
		{
			state->cdc_reading = cc_false;
			break;
		}
	}
}

void CDC_Initialise(CDC_State* const state)
{
	state->current_sector = 0;
	state->sectors_remaining = 0;
	state->host_data_word_index = CDC_END(state);
	state->dma_address = 0;
	state->host_data_buffered_sector_index = 0;
	state->buffered_sectors_read_index = 0;
	state->buffered_sectors_write_index = 0;
	state->buffered_sectors_total = 0;
	state->device_destination = CDC_DESTINATION_SUB_CPU_READ;
	state->host_data_target_sub_cpu = cc_false;
	state->cdc_reading = cc_false;
	state->host_data_bound = cc_false;
}

void CDC_Start(CDC_State* const state, const CDC_SectorReadCallback callback, const void* const user_data)
{
	state->cdc_reading = cc_true;

	RefillSectorBuffer(state, callback, user_data);
}

void CDC_Stop(CDC_State* const state)
{
	state->cdc_reading = cc_false;
}

cc_bool CDC_Stat(CDC_State* const state, const CDC_SectorReadCallback callback, const void* const user_data)
{
	RefillSectorBuffer(state, callback, user_data);

	return state->buffered_sectors_total != 0;
}

cc_bool CDC_Read(CDC_State* const state, const CDC_SectorReadCallback callback, const void* const user_data, cc_u32l* const header)
{
	RefillSectorBuffer(state, callback, user_data);

	if (state->buffered_sectors_total == 0)
		return cc_false;

	if (state->host_data_bound)
		return cc_false;

	/* TODO: Is this thing actually latched during 'CDCRead', or is it when the value is first written? */
	switch (state->device_destination)
	{
		case CDC_DESTINATION_MAIN_CPU_READ:
			state->host_data_target_sub_cpu = cc_false;
			break;

		case CDC_DESTINATION_SUB_CPU_READ:
		case CDC_DESTINATION_PCM_RAM:
		case CDC_DESTINATION_PRG_RAM:
		case CDC_DESTINATION_WORD_RAM:
			state->host_data_target_sub_cpu = cc_true;
			break;

		default:
			LogMessage("CDCREAD called with invalid device destination (0x%" CC_PRIXLEAST8 ")", state->device_destination);
			return cc_false;
	}

	state->host_data_buffered_sector_index = state->buffered_sectors_read_index;
	state->host_data_word_index = 0;

	*header = U16sToU32(state->buffered_sectors[state->host_data_buffered_sector_index]);

	state->host_data_bound = cc_true;

	return cc_true;
}

cc_u16f CDC_HostData(CDC_State* const state, const cc_bool is_sub_cpu)
{
	cc_u16f value;

	if (is_sub_cpu != state->host_data_target_sub_cpu)
	{
		/* TODO: What is actually returned when this is not the target CPU? */
		value = 0;
	}
	else if (!state->host_data_bound)
	{
		/* TODO: What is actually returned in this case? */
		value = 0;
	}
	else if (!DataSetReady(state))
	{
		/* According to Genesis Plus GX, this will repeat the final value indefinitely. */
		/* TODO: Verify this on actual hardware. */
		value = state->buffered_sectors[state->host_data_buffered_sector_index][state->host_data_word_index - 1];
	}
	else
	{
		value = state->buffered_sectors[state->host_data_buffered_sector_index][state->host_data_word_index++];
	}

	return value;
}

void CDC_Ack(CDC_State* const state)
{
	if (!state->host_data_bound)
		return;

	state->host_data_bound = cc_false;

	/* Advance the read index. */
	--state->buffered_sectors_total;

	++state->buffered_sectors_read_index;
	if (state->buffered_sectors_read_index == CC_COUNT_OF(state->buffered_sectors))
		state->buffered_sectors_read_index = 0;
}

void CDC_Seek(CDC_State* const state, const CDC_SectorReadCallback callback, const void* const user_data, const cc_u32f sector, const cc_u32f total_sectors)
{
	state->current_sector = sector;
	state->sectors_remaining = total_sectors;

	RefillSectorBuffer(state, callback, user_data);
}

cc_u16f CDC_Mode(CDC_State* const state, const cc_bool is_sub_cpu)
{
	if (is_sub_cpu != state->host_data_target_sub_cpu)
		return 0x8000;

	return EndOfDataTransfer(state) << 15 | DataSetReady(state) << 14;
}

void CDC_SetDeviceDestination(CDC_State* const state, const CDC_DeviceDestination device_destination)
{
	state->device_destination = device_destination;
	state->dma_address = 0;
}

void CDC_SetDMAAddress(CDC_State* const state, const cc_u16f dma_address)
{
	state->dma_address = dma_address;
}
