#include "cdc.h"

#include "log.h"

static cc_u8f To2DigitBCD(const cc_u8f value)
{
	const cc_u8f lower_digit = value % 10;
	const cc_u8f upper_digit = (value / 10) % 10;
	return (upper_digit << 4) | (lower_digit << 0);
}

static void GetCDSectorHeaderBytes(const CDC* const cdc, cc_u8l* const buffer)
{
	buffer[0] = To2DigitBCD(cdc->current_sector / (75 * 60));
	buffer[1] = To2DigitBCD((cdc->current_sector / 75) % 60);
	buffer[2] = To2DigitBCD(cdc->current_sector % 75);
	/* TODO: Is this byte correct? */
	buffer[3] = 0x01;
}

/* TODO: De-duplicate this 'bytes to integer' logic. */
static cc_u16f BytesToU16(const cc_u8l* const bytes)
{
	return (cc_u16f)bytes[0] << 8 | bytes[1];
}

static cc_u16f U16sToU32(const cc_u16l* const u16s)
{
	return (cc_u32f)u16s[0] << 16 | u16s[1];
}

static void RefillSectorBuffer(CDC* const cdc, const CDC_SectorReadCallback cd_sector_read, const void* const user_data)
{
	if (!cdc->cdc_reading)
		return;

	/* TODO: Stop reading sectors instantaneously! */
	while (cdc->buffered_sectors_total != CC_COUNT_OF(cdc->buffered_sectors))
	{
		cc_u8l header_bytes[4];
		const cc_u8l* sector_bytes = cd_sector_read((void*)user_data);
		cc_u16l* sector_words = cdc->buffered_sectors[cdc->buffered_sectors_write_index];
		cc_u16f i;

		GetCDSectorHeaderBytes(cdc, header_bytes);

		*sector_words++ = BytesToU16(&header_bytes[0]);
		*sector_words++ = BytesToU16(&header_bytes[2]);

		for (i = 0; i < CDC_SECTOR_SIZE; i += 2)
			*sector_words++ = BytesToU16(&sector_bytes[i]);

		++cdc->current_sector;

		++cdc->buffered_sectors_total;
		++cdc->buffered_sectors_write_index;

		if (cdc->buffered_sectors_write_index == CC_COUNT_OF(cdc->buffered_sectors))
			cdc->buffered_sectors_write_index = 0;

		if (cdc->sectors_remaining != 0 && --cdc->sectors_remaining == 0)
		{
			cdc->cdc_reading = cc_false;
			break;
		}
	}
}

void CDC_Initialise(CDC* const cdc)
{
	cdc->current_sector = 0;
	cdc->sectors_remaining = 0;
	cdc->host_data_word_index = CC_COUNT_OF(cdc->buffered_sectors[0]);
	cdc->host_data_buffered_sector_index = 0;
	cdc->buffered_sectors_read_index = 0;
	cdc->buffered_sectors_write_index = 0;
	cdc->buffered_sectors_total = 0;
	cdc->device_destination = 3;
	cdc->host_data_target_sub_cpu = cc_false;
	cdc->cdc_reading = cc_false;
	cdc->host_data_bound = cc_false;
}

void CDC_Start(CDC* const cdc, const CDC_SectorReadCallback callback, const void* const user_data)
{
	cdc->cdc_reading = cc_true;

	RefillSectorBuffer(cdc, callback, user_data);
}

void CDC_Stop(CDC* const cdc)
{
	cdc->cdc_reading = cc_false;
}

cc_bool CDC_Stat(CDC* const cdc, const CDC_SectorReadCallback callback, const void* const user_data)
{
	RefillSectorBuffer(cdc, callback, user_data);

	return cdc->buffered_sectors_total != 0;
}

cc_bool CDC_Read(CDC* const cdc, const CDC_SectorReadCallback callback, const void* const user_data, cc_u32l* const header)
{
	RefillSectorBuffer(cdc, callback, user_data);

	if (cdc->buffered_sectors_total == 0)
		return cc_false;

	if (cdc->host_data_bound)
		return cc_false;

	/* TODO: Use an enum for this. */
	/* TODO: DMA transfers. */
	/* TODO: Log error when invalid. */
	switch (cdc->device_destination)
	{
		case 2:
			cdc->host_data_target_sub_cpu = cc_false;
			break;

		case 3:
			cdc->host_data_target_sub_cpu = cc_true;
			break;

		default:
			LogMessage("CDCREAD called with invalid device destination (%0xX)", cdc->device_destination);
			return cc_false;
	}

	cdc->host_data_buffered_sector_index = cdc->buffered_sectors_read_index;
	cdc->host_data_word_index = 0;

	*header = U16sToU32(cdc->buffered_sectors[cdc->host_data_buffered_sector_index]);

	--cdc->buffered_sectors_total;

	++cdc->buffered_sectors_read_index;
	if (cdc->buffered_sectors_read_index == CC_COUNT_OF(cdc->buffered_sectors))
		cdc->buffered_sectors_read_index = 0;

	cdc->host_data_bound = cc_true;

	return cc_true;
}

cc_u16f CDC_HostData(CDC* const cdc, const cc_bool is_sub_cpu)
{
	if (cdc->host_data_word_index == CC_COUNT_OF(cdc->buffered_sectors[0]))
		return 0; /* TODO: What is actually returned upon data exhaustion? */

	if (is_sub_cpu != cdc->host_data_target_sub_cpu)
		return 0; /* TODO: What is actually returned when the is not the target CPU? */
	
	if (!cdc->host_data_bound)
		return 0;

	return cdc->buffered_sectors[cdc->host_data_buffered_sector_index][cdc->host_data_word_index++];
}

void CDC_Ack(CDC* const cdc, const CDC_SectorReadCallback callback, const void* const user_data)
{
	cdc->host_data_bound = cc_false;
}

void CDC_Seek(CDC* const cdc, const CDC_SectorReadCallback callback, const void* const user_data, const cc_u32f sector, const cc_u32f total_sectors)
{
	cdc->current_sector = sector;
	cdc->sectors_remaining = total_sectors;

	RefillSectorBuffer(cdc, callback, user_data);
}

cc_u16f CDC_Mode(CDC* const cdc, const cc_bool is_sub_cpu)
{
	if (is_sub_cpu != cdc->host_data_target_sub_cpu || cdc->host_data_word_index == CC_COUNT_OF(cdc->buffered_sectors[0]))
		return 0x8000;

	return 0x4000;
}

void CDC_SetDeviceDestination(CDC* const cdc, const cc_u16f device_destination)
{
	/* TODO: Use an enum for this. */
	cdc->device_destination = device_destination;
}
