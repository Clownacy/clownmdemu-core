#include "io-port.h"

#include <string.h>

void IOPort_Initialise(IOPort* const io_port)
{
	/* The standard Sega SDK bootcode uses this to detect soft-resets
	   (it checks if the control value is 0. */
	memset(io_port, 0, sizeof(*io_port));
}

cc_u8f IOPort_ReadData(IOPort* const io_port, const cc_u16f cycles, const IOPort_ReadCallback read_callback, const void *user_data)
{
	if (read_callback == NULL)
		return 0;

	return (read_callback((void*)user_data, cycles) & ~io_port->mask) | io_port->cached_write;
}

void IOPort_WriteData(IOPort* const io_port, const cc_u8f value, const cc_u16f cycles, const IOPort_WriteCallback write_callback, const void *user_data)
{
	if (write_callback == NULL)
		return;

	io_port->cached_write = value & io_port->mask; /* TODO: Is this really how the real thing does this? */
	write_callback((void*)user_data, io_port->cached_write, cycles);
}
