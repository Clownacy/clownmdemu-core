/*
Useful:
https://floooh.github.io/2021/12/06/z80-instruction-timing.html
*/

/* TODO: R and I registers. */

#include "z80.h"

#include <assert.h>
#include <stddef.h>

#include "clowncommon/clowncommon.h"

#include "log.h"

typedef enum InstructionMode
{
	INSTRUCTION_MODE_NORMAL,
	INSTRUCTION_MODE_BITS,
	INSTRUCTION_MODE_MISC
} InstructionMode;

enum
{
	FLAG_BIT_CARRY = 0,
	FLAG_BIT_ADD_SUBTRACT = 1,
	FLAG_BIT_PARITY_OVERFLOW = 2,
	FLAG_BIT_HALF_CARRY = 4,
	FLAG_BIT_ZERO = 6,
	FLAG_BIT_SIGN = 7,
	FLAG_MASK_CARRY = 1 << FLAG_BIT_CARRY,
	FLAG_MASK_ADD_SUBTRACT = 1 << FLAG_BIT_ADD_SUBTRACT,
	FLAG_MASK_PARITY_OVERFLOW = 1 << FLAG_BIT_PARITY_OVERFLOW,
	FLAG_MASK_HALF_CARRY = 1 << FLAG_BIT_HALF_CARRY,
	FLAG_MASK_ZERO = 1 << FLAG_BIT_ZERO,
	FLAG_MASK_SIGN = 1 << FLAG_BIT_SIGN
};

typedef struct Z80Instruction
{
#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
	const Z80_InstructionMetadata *metadata;
#else
	Z80_InstructionMetadata *metadata;
#endif
	cc_u16f literal;
	cc_u16f address;
	cc_bool double_prefix_mode;
} Z80Instruction;

static cc_bool EvaluateCondition(const cc_u8l flags, const Z80_Condition condition)
{
	switch (condition)
	{
		case Z80_CONDITION_NOT_ZERO:
			return (flags & FLAG_MASK_ZERO) == 0;

		case Z80_CONDITION_ZERO:
			return (flags & FLAG_MASK_ZERO) != 0;

		case Z80_CONDITION_NOT_CARRY:
			return (flags & FLAG_MASK_CARRY) == 0;

		case Z80_CONDITION_CARRY:
			return (flags & FLAG_MASK_CARRY) != 0;

		case Z80_CONDITION_PARITY_OVERFLOW:
			return (flags & FLAG_MASK_PARITY_OVERFLOW) == 0;

		case Z80_CONDITION_PARITY_EQUALITY:
			return (flags & FLAG_MASK_PARITY_OVERFLOW) != 0;

		case Z80_CONDITION_PLUS:
			return (flags & FLAG_MASK_SIGN) == 0;

		case Z80_CONDITION_MINUS:
			return (flags & FLAG_MASK_SIGN) != 0;

		default:
			/* Should never occur. */
			assert(0);
			return cc_false;
	}
}

static cc_u16f MemoryRead(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address)
{
	/* Memory accesses take 3 cycles. */
	z80->state->cycles += 3;

	return callbacks->read(callbacks->user_data, address);
}

static void MemoryWrite(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address, const cc_u16f data)
{
	/* Memory accesses take 3 cycles. */
	z80->state->cycles += 3;

	callbacks->write(callbacks->user_data, address, data);
}

static cc_u16f InstructionMemoryRead(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks)
{
	const cc_u16f data = MemoryRead(z80, callbacks, z80->state->program_counter);

	++z80->state->program_counter;
	z80->state->program_counter &= 0xFFFF;

	return data;
}

static cc_u16f OpcodeFetch(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks)
{
	/* Opcode fetches take an extra cycle. */
	++z80->state->cycles;

	/* Increment the lower 7 bits of the 'R' register. */
	z80->state->r = (z80->state->r & 0x80) | ((z80->state->r + 1) & 0x7F);

	return InstructionMemoryRead(z80, callbacks);
}

/* TODO: Should the bytes be written in reverse order? */
static cc_u16f MemoryRead16Bit(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address)
{
	cc_u16f value;
	value = MemoryRead(z80, callbacks, address + 0);
	value |= MemoryRead(z80, callbacks, (address + 1) & 0xFFFF) << 8;
	return value;
}

static void MemoryWrite16Bit(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const cc_u16f address, const cc_u16f value)
{
	MemoryWrite(z80, callbacks, address + 0, value & 0xFF);
	MemoryWrite(z80, callbacks, (address + 1) & 0xFFFF, value >> 8);
}

static cc_u16f ReadOperand(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const Z80Instruction* const instruction, Z80_Operand operand)
{
	cc_u16f value;

	/* Handle double-prefix instructions. */
	/* Technically, this is only relevant to the destination operand and not the source operand,
	   but double-prefix instructions don't have a source operand, so this is not a problem. */
	if (instruction->double_prefix_mode)
		operand = z80->state->register_mode == Z80_REGISTER_MODE_IX ? Z80_OPERAND_IX_INDIRECT : Z80_OPERAND_IY_INDIRECT;

	switch (operand)
	{
		default:
		case Z80_OPERAND_NONE:
			/* Should never happen. */
			assert(0);
			/* Fallthrough */
		case Z80_OPERAND_A:
			value = z80->state->a;
			break;

		case Z80_OPERAND_B:
			value = z80->state->b;
			break;

		case Z80_OPERAND_C:
			value = z80->state->c;
			break;

		case Z80_OPERAND_D:
			value = z80->state->d;
			break;

		case Z80_OPERAND_E:
			value = z80->state->e;
			break;

		case Z80_OPERAND_H:
			value = z80->state->h;
			break;

		case Z80_OPERAND_L:
			value = z80->state->l;
			break;

		case Z80_OPERAND_IXH:
			value = z80->state->ixh;
			break;

		case Z80_OPERAND_IXL:
			value = z80->state->ixl;
			break;

		case Z80_OPERAND_IYH:
			value = z80->state->iyh;
			break;

		case Z80_OPERAND_IYL:
			value = z80->state->iyl;
			break;

		case Z80_OPERAND_AF:
			value = ((cc_u16f)z80->state->a << 8) | z80->state->f;
			break;

		case Z80_OPERAND_BC:
			value = ((cc_u16f)z80->state->b << 8) | z80->state->c;
			break;

		case Z80_OPERAND_DE:
			value = ((cc_u16f)z80->state->d << 8) | z80->state->e;
			break;

		case Z80_OPERAND_HL:
			value = ((cc_u16f)z80->state->h << 8) | z80->state->l;
			break;

		case Z80_OPERAND_IX:
			value = ((cc_u16f)z80->state->ixh << 8) | z80->state->ixl;
			break;

		case Z80_OPERAND_IY:
			value = ((cc_u16f)z80->state->iyh << 8) | z80->state->iyl;
			break;

		case Z80_OPERAND_PC:
			value = z80->state->program_counter;
			break;

		case Z80_OPERAND_SP:
			value = z80->state->stack_pointer;
			break;

		case Z80_OPERAND_LITERAL_8BIT:
		case Z80_OPERAND_LITERAL_16BIT:
			value = instruction->literal;
			break;

		case Z80_OPERAND_BC_INDIRECT:
		case Z80_OPERAND_DE_INDIRECT:
		case Z80_OPERAND_HL_INDIRECT:
		case Z80_OPERAND_IX_INDIRECT:
		case Z80_OPERAND_IY_INDIRECT:
		case Z80_OPERAND_ADDRESS:
			value = MemoryRead(z80, callbacks, instruction->address);

			if (instruction->metadata->opcode == Z80_OPCODE_LD_16BIT)
				value |= MemoryRead(z80, callbacks, instruction->address + 1) << 8;

			break;
	}

	return value;
}

static void WriteOperand(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const Z80Instruction* const instruction, const Z80_Operand operand, const cc_u16f value)
{
	/* Handle double-prefix instructions. */
	const Z80_Operand double_prefix_operand = z80->state->register_mode == Z80_REGISTER_MODE_IX ? Z80_OPERAND_IX_INDIRECT : Z80_OPERAND_IY_INDIRECT;

	/* Don't do a redundant write in double-prefix mode when operating on '(IX+*)' or (IY+*). */
	if (instruction->double_prefix_mode && operand != double_prefix_operand)
		WriteOperand(z80, callbacks, instruction, double_prefix_operand, value);

	switch (operand)
	{
		default:
		case Z80_OPERAND_NONE:
		case Z80_OPERAND_LITERAL_8BIT:
		case Z80_OPERAND_LITERAL_16BIT:
			/* Should never happen. */
			assert(0);
			break;

		case Z80_OPERAND_A:
			z80->state->a = value;
			break;

		case Z80_OPERAND_B:
			z80->state->b = value;
			break;

		case Z80_OPERAND_C:
			z80->state->c = value;
			break;

		case Z80_OPERAND_D:
			z80->state->d = value;
			break;

		case Z80_OPERAND_E:
			z80->state->e = value;
			break;

		case Z80_OPERAND_H:
			z80->state->h = value;
			break;

		case Z80_OPERAND_L:
			z80->state->l = value;
			break;

		case Z80_OPERAND_IXH:
			z80->state->ixh = value;
			break;

		case Z80_OPERAND_IXL:
			z80->state->ixl = value;
			break;

		case Z80_OPERAND_IYH:
			z80->state->iyh = value;
			break;

		case Z80_OPERAND_IYL:
			z80->state->iyl = value;
			break;

		case Z80_OPERAND_AF:
			z80->state->a = value >> 8;
			z80->state->f = value & 0xFF;
			break;

		case Z80_OPERAND_BC:
			z80->state->b = value >> 8;
			z80->state->c = value & 0xFF;
			break;

		case Z80_OPERAND_DE:
			z80->state->d = value >> 8;
			z80->state->e = value & 0xFF;
			break;

		case Z80_OPERAND_HL:
			z80->state->h = value >> 8;
			z80->state->l = value & 0xFF;
			break;

		case Z80_OPERAND_IX:
			z80->state->ixh = value >> 8;
			z80->state->ixl = value & 0xFF;
			break;

		case Z80_OPERAND_IY:
			z80->state->iyh = value >> 8;
			z80->state->iyl = value & 0xFF;
			break;

		case Z80_OPERAND_PC:
			z80->state->program_counter = value;
			break;

		case Z80_OPERAND_SP:
			z80->state->stack_pointer = value;
			break;

		case Z80_OPERAND_BC_INDIRECT:
		case Z80_OPERAND_DE_INDIRECT:
		case Z80_OPERAND_HL_INDIRECT:
		case Z80_OPERAND_IX_INDIRECT:
		case Z80_OPERAND_IY_INDIRECT:
		case Z80_OPERAND_ADDRESS:
			if (instruction->metadata->opcode == Z80_OPCODE_LD_16BIT)
				MemoryWrite16Bit(z80, callbacks, instruction->address, value);
			else
				MemoryWrite(z80, callbacks, instruction->address, value);

			break;
	}
}

static void DecodeInstructionMetadata(Z80_InstructionMetadata* const metadata, const InstructionMode instruction_mode, const Z80_RegisterMode register_mode, const cc_u8l opcode)
{
	static const Z80_Operand registers[8] = {Z80_OPERAND_B, Z80_OPERAND_C, Z80_OPERAND_D, Z80_OPERAND_E, Z80_OPERAND_H, Z80_OPERAND_L, Z80_OPERAND_HL_INDIRECT, Z80_OPERAND_A};
	static const Z80_Operand register_pairs_1[4] = {Z80_OPERAND_BC, Z80_OPERAND_DE, Z80_OPERAND_HL, Z80_OPERAND_SP};
	static const Z80_Operand register_pairs_2[4] = {Z80_OPERAND_BC, Z80_OPERAND_DE, Z80_OPERAND_HL, Z80_OPERAND_AF};
	static const Z80_Opcode arithmetic_logic_opcodes[8] = {Z80_OPCODE_ADD_A, Z80_OPCODE_ADC_A, Z80_OPCODE_SUB, Z80_OPCODE_SBC_A, Z80_OPCODE_AND, Z80_OPCODE_XOR, Z80_OPCODE_OR, Z80_OPCODE_CP};
	static const Z80_Opcode rotate_shift_opcodes[8] = {Z80_OPCODE_RLC, Z80_OPCODE_RRC, Z80_OPCODE_RL, Z80_OPCODE_RR, Z80_OPCODE_SLA, Z80_OPCODE_SRA, Z80_OPCODE_SLL, Z80_OPCODE_SRL};
	static const Z80_Opcode block_opcodes[4][4] = {
		{Z80_OPCODE_LDI,  Z80_OPCODE_LDD,  Z80_OPCODE_LDIR, Z80_OPCODE_LDDR},
		{Z80_OPCODE_CPI,  Z80_OPCODE_CPD,  Z80_OPCODE_CPIR, Z80_OPCODE_CPDR},
		{Z80_OPCODE_INI,  Z80_OPCODE_IND,  Z80_OPCODE_INIR, Z80_OPCODE_INDR},
		{Z80_OPCODE_OUTI, Z80_OPCODE_OUTD, Z80_OPCODE_OTIR, Z80_OPCODE_OTDR}
	};

	const cc_u16f x = (opcode >> 6) & 3;
	const cc_u16f y = (opcode >> 3) & 7;
	const cc_u16f z = (opcode >> 0) & 7;
	const cc_u16f p = (y >> 1) & 3;
	const cc_bool q = (y & 1) != 0;

	cc_u16f i;

	metadata->has_displacement = cc_false;

	metadata->operands[0] = Z80_OPERAND_NONE;
	metadata->operands[1] = Z80_OPERAND_NONE;

	switch (instruction_mode)
	{
		case INSTRUCTION_MODE_NORMAL:
			switch (x)
			{
				case 0:
					switch (z)
					{
						case 0:
							switch (y)
							{
								case 0:
									metadata->opcode = Z80_OPCODE_NOP;
									break;

								case 1:
									metadata->opcode = Z80_OPCODE_EX_AF_AF;
									break;

								case 2:
									metadata->opcode = Z80_OPCODE_DJNZ;
									metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
									break;

								case 3:
									metadata->opcode = Z80_OPCODE_JR_UNCONDITIONAL;
									metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
									break;

								case 4:
								case 5:
								case 6:
								case 7:
									metadata->opcode = Z80_OPCODE_JR_CONDITIONAL;
									metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
									metadata->condition = y - 4;
									break;
							}

							break;

						case 1:
							if (!q)
							{
								metadata->opcode = Z80_OPCODE_LD_16BIT;
								metadata->operands[0] = Z80_OPERAND_LITERAL_16BIT;
								metadata->operands[1] = register_pairs_1[p];
							}
							else
							{
								metadata->opcode = Z80_OPCODE_ADD_HL;
								metadata->operands[0] = register_pairs_1[p];
								metadata->operands[1] = Z80_OPERAND_HL;
							}

							break;

						case 2:
						{
							static const Z80_Operand operands[4] = {Z80_OPERAND_BC_INDIRECT, Z80_OPERAND_DE_INDIRECT, Z80_OPERAND_ADDRESS, Z80_OPERAND_ADDRESS};

							const Z80_Operand operand_a = p == 2 ? Z80_OPERAND_HL : Z80_OPERAND_A;
							const Z80_Operand operand_b = operands[p];

							metadata->opcode = p == 2 ? Z80_OPCODE_LD_16BIT : Z80_OPCODE_LD_8BIT;

							if (!q)
							{
								metadata->operands[0] = operand_a;
								metadata->operands[1] = operand_b;
							}
							else
							{
								metadata->operands[0] = operand_b;
								metadata->operands[1] = operand_a;
							}

							break;
						}

						case 3:
							if (!q)
								metadata->opcode = Z80_OPCODE_INC_16BIT;
							else
								metadata->opcode = Z80_OPCODE_DEC_16BIT;

							metadata->operands[1] = register_pairs_1[p];
							break;

						case 4:
							metadata->opcode = Z80_OPCODE_INC_8BIT;
							metadata->operands[1] = registers[y];
							break;

						case 5:
							metadata->opcode = Z80_OPCODE_DEC_8BIT;
							metadata->operands[1] = registers[y];
							break;

						case 6:
							metadata->opcode = Z80_OPCODE_LD_8BIT;
							metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
							metadata->operands[1] = registers[y];
							break;

						case 7:
						{
							static const Z80_Opcode opcodes[8] = {Z80_OPCODE_RLCA, Z80_OPCODE_RRCA, Z80_OPCODE_RLA, Z80_OPCODE_RRA, Z80_OPCODE_DAA, Z80_OPCODE_CPL, Z80_OPCODE_SCF, Z80_OPCODE_CCF};
							metadata->opcode = opcodes[y];
							break;
						}
					}

					break;

				case 1:
					if (z == 6 && y == 6)
					{
						metadata->opcode = Z80_OPCODE_HALT;
					}
					else
					{
						metadata->opcode = Z80_OPCODE_LD_8BIT;
						metadata->operands[0] = registers[z];
						metadata->operands[1] = registers[y];
					}

					break;

				case 2:
					metadata->opcode = arithmetic_logic_opcodes[y];
					metadata->operands[0] = registers[z];
					break;

				case 3:
					switch (z)
					{
						case 0:
							metadata->opcode = Z80_OPCODE_RET_CONDITIONAL;
							metadata->condition = y;
							break;

						case 1:
							if (!q)
							{
								metadata->opcode = Z80_OPCODE_POP;
								metadata->operands[1] = register_pairs_2[p];
							}
							else
							{
								switch (p)
								{
									case 0:
										metadata->opcode = Z80_OPCODE_RET_UNCONDITIONAL;
										break;

									case 1:
										metadata->opcode = Z80_OPCODE_EXX;
										break;

									case 2:
										metadata->opcode = Z80_OPCODE_JP_HL;
										metadata->operands[0] = Z80_OPERAND_HL;
										break;

									case 3:
										metadata->opcode = Z80_OPCODE_LD_SP_HL;
										metadata->operands[0] = Z80_OPERAND_HL;
										break;
								}
							}

							break;

						case 2:
							metadata->opcode = Z80_OPCODE_JP_CONDITIONAL;
							metadata->condition = y;
							metadata->operands[0] = Z80_OPERAND_LITERAL_16BIT;
							break;

						case 3:
							switch (y)
							{
								case 0:
									metadata->opcode = Z80_OPCODE_JP_UNCONDITIONAL;
									metadata->operands[0] = Z80_OPERAND_LITERAL_16BIT;
									break;

								case 1:
									metadata->opcode = Z80_OPCODE_CB_PREFIX;

									if (register_mode != Z80_REGISTER_MODE_HL)
										metadata->has_displacement = cc_true;

									break;

								case 2:
									metadata->opcode = Z80_OPCODE_OUT;
									metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
									break;

								case 3:
									metadata->opcode = Z80_OPCODE_IN;
									metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
									break;

								case 4:
									metadata->opcode = Z80_OPCODE_EX_SP_HL;
									metadata->operands[1] = Z80_OPERAND_HL;
									break;

								case 5:
									metadata->opcode = Z80_OPCODE_EX_DE_HL;
									break;

								case 6:
									metadata->opcode = Z80_OPCODE_DI;
									break;

								case 7:
									metadata->opcode = Z80_OPCODE_EI;
									break;
							}
					
							break;

						case 4:
							metadata->opcode = Z80_OPCODE_CALL_CONDITIONAL;
							metadata->condition = y;
							metadata->operands[0] = Z80_OPERAND_LITERAL_16BIT;
							break;

						case 5:
							if (!q)
							{
								metadata->opcode = Z80_OPCODE_PUSH;
								metadata->operands[0] = register_pairs_2[p];
							}
							else
							{
								switch (p)
								{
									case 0:
										metadata->opcode = Z80_OPCODE_CALL_UNCONDITIONAL;
										metadata->operands[0] = Z80_OPERAND_LITERAL_16BIT;
										break;

									case 1:
										metadata->opcode = Z80_OPCODE_DD_PREFIX;
										break;

									case 2:
										metadata->opcode = Z80_OPCODE_ED_PREFIX;
										break;

									case 3:
										metadata->opcode = Z80_OPCODE_FD_PREFIX;
										break;
								}
							}

							break;

						case 6:
							metadata->opcode = arithmetic_logic_opcodes[y];
							metadata->operands[0] = Z80_OPERAND_LITERAL_8BIT;
							break;

						case 7:
							metadata->opcode = Z80_OPCODE_RST;
							metadata->embedded_literal = y * 8;
							break;
					}

					break;
			}

			break;

		case INSTRUCTION_MODE_BITS:
			switch (x)
			{
				case 0:
					metadata->opcode = rotate_shift_opcodes[y];
					metadata->operands[1] = registers[z];
					break;

				case 1:
					metadata->opcode = Z80_OPCODE_BIT;
					metadata->operands[1] = registers[z];
					metadata->embedded_literal = 1u << y;
					break;

				case 2:
					metadata->opcode = Z80_OPCODE_RES;
					metadata->operands[1] = registers[z];
					metadata->embedded_literal = ~(1u << y);
					break;

				case 3:
					metadata->opcode = Z80_OPCODE_SET;
					metadata->operands[1] = registers[z];
					metadata->embedded_literal = 1u << y;
					break;
			}

			break;

		case INSTRUCTION_MODE_MISC:
			switch (x)
			{
				case 0:
				case 3:
					/* Invalid instruction. */
					metadata->opcode = Z80_OPCODE_NOP;
					break;

				case 1:
					switch (z)
					{
						case 0:
							if (y != 6)
								metadata->opcode = Z80_OPCODE_IN_REGISTER;
							else
								metadata->opcode = Z80_OPCODE_IN_NO_REGISTER;

							break;

						case 1:
							if (y != 6)
								metadata->opcode = Z80_OPCODE_OUT_REGISTER;
							else
								metadata->opcode = Z80_OPCODE_OUT_NO_REGISTER;

							break;

						case 2:
							if (!q)
								metadata->opcode = Z80_OPCODE_SBC_HL;
							else
								metadata->opcode = Z80_OPCODE_ADC_HL;

							metadata->operands[0] = register_pairs_1[p];
							metadata->operands[1] = Z80_OPERAND_HL;

							break;

						case 3:
							metadata->opcode = Z80_OPCODE_LD_16BIT;

							if (!q)
							{
								metadata->operands[0] = register_pairs_1[p];
								metadata->operands[1] = Z80_OPERAND_ADDRESS;
							}
							else
							{
								metadata->operands[0] = Z80_OPERAND_ADDRESS;
								metadata->operands[1] = register_pairs_1[p];
							}

							break;

						case 4:
							metadata->opcode = Z80_OPCODE_NEG;
							metadata->operands[0] = Z80_OPERAND_A;
							metadata->operands[1] = Z80_OPERAND_A;
							break;

						case 5:
							if (y != 1)
								metadata->opcode = Z80_OPCODE_RETN;
							else
								metadata->opcode = Z80_OPCODE_RETI;

							break;

						case 6:
						{
							static const cc_u16f interrupt_modes[4] = {0, 0, 1, 2};

							metadata->opcode = Z80_OPCODE_IM;
							metadata->embedded_literal = interrupt_modes[y & 3];

							break;
						}

						case 7:
						{
							static const Z80_Opcode assorted_opcodes[8] = {Z80_OPCODE_LD_I_A, Z80_OPCODE_LD_R_A, Z80_OPCODE_LD_A_I, Z80_OPCODE_LD_A_R, Z80_OPCODE_RRD, Z80_OPCODE_RLD, Z80_OPCODE_NOP, Z80_OPCODE_NOP};
							metadata->opcode = assorted_opcodes[y];
							break;
						}
					}

					break;

				case 2:
					if (z <= 3 && y >= 4)
						metadata->opcode = block_opcodes[z][y - 4];
					else
						/* Invalid instruction. */
						metadata->opcode = Z80_OPCODE_NOP;

					break;
			}
			break;
	}

	/* Convert HL to IX and IY if needed. */
	for (i = 0; i < 2; ++i)
	{
		if (metadata->operands[i ^ 1] != Z80_OPERAND_HL_INDIRECT
		 && metadata->operands[i ^ 1] != Z80_OPERAND_IX_INDIRECT
		 && metadata->operands[i ^ 1] != Z80_OPERAND_IY_INDIRECT)
		{
			switch (metadata->operands[i])
			{
				default:
					break;

				case Z80_OPERAND_H:
					switch (register_mode)
					{
						case Z80_REGISTER_MODE_HL:
							break;

						case Z80_REGISTER_MODE_IX:
							metadata->operands[i] = Z80_OPERAND_IXH;
							break;

						case Z80_REGISTER_MODE_IY:
							metadata->operands[i] = Z80_OPERAND_IYH;
							break;
					}

					break;

				case Z80_OPERAND_L:
					switch (register_mode)
					{
						case Z80_REGISTER_MODE_HL:
							break;

						case Z80_REGISTER_MODE_IX:
							metadata->operands[i] = Z80_OPERAND_IXL;
							break;

						case Z80_REGISTER_MODE_IY:
							metadata->operands[i] = Z80_OPERAND_IYL;
							break;
					}

					break;

				case Z80_OPERAND_HL:
					switch (register_mode)
					{
						case Z80_REGISTER_MODE_HL:
							break;

						case Z80_REGISTER_MODE_IX:
							metadata->operands[i] = Z80_OPERAND_IX;
							break;

						case Z80_REGISTER_MODE_IY:
							metadata->operands[i] = Z80_OPERAND_IY;
							break;
					}

					break;

				case Z80_OPERAND_HL_INDIRECT:
					switch (register_mode)
					{
						case Z80_REGISTER_MODE_HL:
							break;

						case Z80_REGISTER_MODE_IX:
							metadata->operands[i] = Z80_OPERAND_IX_INDIRECT;
							metadata->has_displacement = cc_true;
							break;

						case Z80_REGISTER_MODE_IY:
							metadata->operands[i] = Z80_OPERAND_IY_INDIRECT;
							metadata->has_displacement = cc_true;
							break;
					}

					break;
			}
		}
	}
}

static void DecodeInstruction(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, Z80Instruction* const instruction)
{
	cc_u16f opcode;
	cc_u16f displacement;
	cc_u16f i;

	opcode = OpcodeFetch(z80, callbacks);

	/* Shut up a 'may be used uninitialised' compiler warning. */
	displacement = 0;

#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
	instruction->metadata = &z80->constant->instruction_metadata_lookup_normal[z80->state->register_mode][opcode];
#else
	DecodeInstructionMetadata(instruction->metadata, INSTRUCTION_MODE_NORMAL, (Z80_RegisterMode)z80->state->register_mode, opcode);
#endif

	/* Obtain displacement byte if one exists. */
	if (instruction->metadata->has_displacement)
	{
		displacement = InstructionMemoryRead(z80, callbacks);
		displacement = CC_SIGN_EXTEND_UINT(7, displacement);

		/* The displacement byte adds 5 cycles on top of the 3 required to read it. */
		z80->state->cycles += 5;
	}

	instruction->double_prefix_mode = cc_false;

	/* Handle prefix instructions. */
	switch ((Z80_Opcode)instruction->metadata->opcode)
	{
		default:
			/* Nothing to do here. */
			break;

		case Z80_OPCODE_CB_PREFIX:
			if (z80->state->register_mode == Z80_REGISTER_MODE_HL)
			{
				/* Normal mode. */
				opcode = OpcodeFetch(z80, callbacks);

			#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
				instruction->metadata = &z80->constant->instruction_metadata_lookup_bits[z80->state->register_mode][opcode];
			#else
				DecodeInstructionMetadata(instruction->metadata, INSTRUCTION_MODE_BITS, (Z80_RegisterMode)z80->state->register_mode, opcode);
			#endif
			}
			else
			{
				/* Double-prefix mode. */
				instruction->double_prefix_mode = cc_true;

				/* Yes, double-prefix mode fetches the opcode with a regular memory read. */
				opcode = InstructionMemoryRead(z80, callbacks);

				/* Reading the opcode is overlaid with the 5 displacement cycles, so the above memory read doesn't cost 3 cycles. */
				z80->state->cycles -= 3;

				if (z80->state->register_mode == Z80_REGISTER_MODE_IX)
					instruction->address = ((((cc_u16f)z80->state->ixh << 8) | z80->state->ixl) + displacement) & 0xFFFF;
				else /*if (z80->state->register_mode == Z80_REGISTER_MODE_IY)*/
					instruction->address = ((((cc_u16f)z80->state->iyh << 8) | z80->state->iyl) + displacement) & 0xFFFF;

				/* TODO: Use a separate lookup for double-prefix mode? */
			#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
				instruction->metadata = &z80->constant->instruction_metadata_lookup_bits[Z80_REGISTER_MODE_HL][opcode];
			#else
				DecodeInstructionMetadata(instruction->metadata, INSTRUCTION_MODE_BITS, Z80_REGISTER_MODE_HL, opcode);
			#endif

				if (instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT)
				#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
					instruction->metadata = &z80->constant->instruction_metadata_lookup_bits[z80->state->register_mode][opcode];
				#else
					DecodeInstructionMetadata(instruction->metadata, INSTRUCTION_MODE_BITS, (Z80_RegisterMode)z80->state->register_mode, opcode);
				#endif
			}

			break;

		case Z80_OPCODE_ED_PREFIX:
			opcode = OpcodeFetch(z80, callbacks);

		#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
			instruction->metadata = &z80->constant->instruction_metadata_lookup_misc[opcode];
		#else
			DecodeInstructionMetadata(instruction->metadata, INSTRUCTION_MODE_MISC, Z80_REGISTER_MODE_HL, opcode);
		#endif

			break;
	}

	/* Obtain literal data. */
	switch ((Z80_Operand)instruction->metadata->operands[0])
	{
		default:
			/* Nothing to do here. */
			break;

		case Z80_OPERAND_LITERAL_8BIT:
			instruction->literal = InstructionMemoryRead(z80, callbacks);

			if (instruction->metadata->has_displacement)
			{
				/* Reading the literal is overlaid with the 5 displacement cycles, so the above memory read doesn't cost 3 cycles. */
				z80->state->cycles -= 3;
			}

			break;

		case Z80_OPERAND_LITERAL_16BIT:
			instruction->literal = InstructionMemoryRead(z80, callbacks);
			instruction->literal |= InstructionMemoryRead(z80, callbacks) << 8;
			break;
	}

	/* Pre-calculate the address of indirect memory operands. */
	for (i = 0; i < 2; ++i)
	{
		switch ((Z80_Operand)instruction->metadata->operands[i])
		{
			default:
				break;

			case Z80_OPERAND_BC_INDIRECT:
				instruction->address = ((cc_u16f)z80->state->b << 8) | z80->state->c;
				break;

			case Z80_OPERAND_DE_INDIRECT:
				instruction->address = ((cc_u16f)z80->state->d << 8) | z80->state->e;
				break;

			case Z80_OPERAND_HL_INDIRECT:
				instruction->address = ((cc_u16f)z80->state->h << 8) | z80->state->l;
				break;

			case Z80_OPERAND_IX_INDIRECT:
				instruction->address = ((((cc_u16f)z80->state->ixh << 8) | z80->state->ixl) + displacement) & 0xFFFF;
				break;

			case Z80_OPERAND_IY_INDIRECT:
				instruction->address = ((((cc_u16f)z80->state->iyh << 8) | z80->state->iyl) + displacement) & 0xFFFF;
				break;

			case Z80_OPERAND_ADDRESS:
				instruction->address = InstructionMemoryRead(z80, callbacks);
				instruction->address |= InstructionMemoryRead(z80, callbacks) << 8;
				break;
		}
	}
}

static cc_bool ComputeParity(cc_u8f value)
{
	value |= value >> 4;
	value |= value >> 2;
	value |= value >> 1;

	return (value & 1) == 0;
}

#define SWAP(a, b) \
	swap_holder = a; \
	a = b; \
	b = swap_holder

#define CONDITION_SIGN_BASE(bit) z80->state->f |= (result_value >> (bit - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN
#define CONDITION_CARRY_BASE(variable, bit) z80->state->f |= (variable >> (bit - FLAG_BIT_CARRY)) & FLAG_MASK_CARRY
#define CONDITION_HALF_CARRY_BASE(bit) z80->state->f |= ((source_value ^ destination_value ^ result_value) >> (bit - FLAG_BIT_HALF_CARRY)) & FLAG_MASK_HALF_CARRY
#define CONDITION_OVERFLOW_BASE(bit) z80->state->f |= ((~(source_value ^ destination_value) & (source_value ^ result_value)) >> (bit - FLAG_BIT_PARITY_OVERFLOW)) & FLAG_MASK_PARITY_OVERFLOW

#define CONDITION_SIGN_16BIT CONDITION_SIGN_BASE(15)
#define CONDITION_HALF_CARRY_16BIT CONDITION_HALF_CARRY_BASE(12)
#define CONDITION_OVERFLOW_16BIT CONDITION_OVERFLOW_BASE(15)
#define CONDITION_CARRY_16BIT CONDITION_CARRY_BASE(result_value_with_carry_16bit, 16)

#define CONDITION_SIGN CONDITION_SIGN_BASE(7)
#define CONDITION_ZERO z80->state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0
#define CONDITION_HALF_CARRY CONDITION_HALF_CARRY_BASE(4)
#define CONDITION_OVERFLOW CONDITION_OVERFLOW_BASE(7)
#define CONDITION_PARITY z80->state->f |= ComputeParity(result_value) ? FLAG_MASK_PARITY_OVERFLOW : 0
#define CONDITION_CARRY CONDITION_CARRY_BASE(result_value_with_carry, 8)

#define READ_SOURCE source_value = ReadOperand(z80, callbacks, instruction, (Z80_Operand)instruction->metadata->operands[0])
#define READ_DESTINATION destination_value = ReadOperand(z80, callbacks, instruction, (Z80_Operand)instruction->metadata->operands[1])

#define WRITE_DESTINATION WriteOperand(z80, callbacks, instruction, (Z80_Operand)instruction->metadata->operands[1], result_value)

static void ExecuteInstruction(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks, const Z80Instruction* const instruction)
{
	cc_u16f source_value;
	cc_u16f destination_value;
	cc_u16f result_value;
	cc_u16f result_value_with_carry;
	cc_u32f result_value_with_carry_16bit;
	cc_u8l swap_holder;
	cc_bool carry;

	z80->state->register_mode = Z80_REGISTER_MODE_HL;

	switch ((Z80_Opcode)instruction->metadata->opcode)
	{
		#define UNIMPLEMENTED_Z80_INSTRUCTION(instruction) LogMessage("Unimplemented instruction " instruction " used at 0x%" CC_PRIXLEAST16, z80->state->program_counter)

		case Z80_OPCODE_NOP:
			/* Does nothing, naturally. */
			break;

		case Z80_OPCODE_EX_AF_AF:
			SWAP(z80->state->a, z80->state->a_);
			SWAP(z80->state->f, z80->state->f_);
			break;

		case Z80_OPCODE_DJNZ:
			/* This instruction takes an extra cycle. */
			z80->state->cycles += 1;

			--z80->state->b;
			z80->state->b &= 0xFF;

			if (z80->state->b != 0)
			{
				z80->state->program_counter += CC_SIGN_EXTEND_UINT(7, instruction->literal);

				/* Branching takes 5 cycles. */
				z80->state->cycles += 5;
			}

			break;

		case Z80_OPCODE_JR_CONDITIONAL:
			if (!EvaluateCondition(z80->state->f, (Z80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case Z80_OPCODE_JR_UNCONDITIONAL:
			z80->state->program_counter += CC_SIGN_EXTEND_UINT(7, instruction->literal);

			/* Branching takes 5 cycles. */
			z80->state->cycles += 5;

			break;

		case Z80_OPCODE_LD_8BIT:
		case Z80_OPCODE_LD_16BIT:
			READ_SOURCE;

			result_value = source_value;

			WRITE_DESTINATION;

			break;

		case Z80_OPCODE_ADD_HL:
			READ_SOURCE;
			READ_DESTINATION;

			result_value_with_carry_16bit = (cc_u32f)source_value + (cc_u32f)destination_value;
			result_value = result_value_with_carry_16bit & 0xFFFF;

			z80->state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;

			CONDITION_CARRY_16BIT;
			CONDITION_HALF_CARRY_16BIT;

			WRITE_DESTINATION;

			/* This instruction requires an extra 7 cycles. */
			z80->state->cycles += 7;

			break;

		case Z80_OPCODE_INC_16BIT:
			READ_DESTINATION;

			result_value = (destination_value + 1) & 0xFFFF;

			WRITE_DESTINATION;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			break;

		case Z80_OPCODE_DEC_16BIT:
			READ_DESTINATION;

			result_value = (destination_value - 1) & 0xFFFF;

			WRITE_DESTINATION;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			break;

		case Z80_OPCODE_INC_8BIT:
			source_value = 1;
			READ_DESTINATION;

			result_value = (destination_value + source_value) & 0xFF;

			z80->state->f &= FLAG_MASK_CARRY;

			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_DEC_8BIT:
			source_value = -1;
			READ_DESTINATION;

			result_value = (destination_value + source_value) & 0xFF;

			z80->state->f &= FLAG_MASK_CARRY;

			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->f ^= FLAG_MASK_HALF_CARRY;
			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_RLCA:
			carry = (z80->state->a & 0x80) != 0;

			z80->state->a <<= 1;
			z80->state->a &= 0xFF;
			z80->state->a |= carry ? 0x01 : 0;

			z80->state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case Z80_OPCODE_RRCA:
			carry = (z80->state->a & 0x01) != 0;

			z80->state->a >>= 1;
			z80->state->a |= carry ? 0x80 : 0;

			z80->state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case Z80_OPCODE_RLA:
			carry = (z80->state->a & 0x80) != 0;

			z80->state->a <<= 1;
			z80->state->a &= 0xFF;
			z80->state->a |= (z80->state->f & FLAG_MASK_CARRY) != 0 ? 1 : 0;

			z80->state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case Z80_OPCODE_RRA:
			carry = (z80->state->a & 0x01) != 0;

			z80->state->a >>= 1;
			z80->state->a |= (z80->state->f & FLAG_MASK_CARRY) != 0 ? 0x80 : 0;

			z80->state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			break;

		case Z80_OPCODE_DAA:
		{
			cc_u16f correction_factor;

			const cc_u16f original_a = z80->state->a;

			correction_factor = ((z80->state->a + 0x66) ^ z80->state->a) & 0x110;
			correction_factor |= (z80->state->f & FLAG_MASK_CARRY) << (8 - FLAG_BIT_CARRY);
			correction_factor |= (z80->state->f & FLAG_MASK_HALF_CARRY) << (4 - FLAG_BIT_HALF_CARRY);
			correction_factor = (correction_factor >> 2) | (correction_factor >> 3);

			if ((z80->state->f & FLAG_MASK_ADD_SUBTRACT) != 0)
				z80->state->a -= correction_factor;
			else
				z80->state->a += correction_factor;

			z80->state->a &= 0xFF;

			z80->state->f &= FLAG_MASK_ADD_SUBTRACT;
			z80->state->f |= (z80->state->a >> (7 - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN;
			z80->state->f |= (z80->state->a == 0) << FLAG_BIT_ZERO;
			z80->state->f |= ((original_a ^ z80->state->a) >> (4 - FLAG_BIT_HALF_CARRY)) & FLAG_MASK_HALF_CARRY; /* Binary carry. */
			z80->state->f |= ComputeParity(z80->state->a) ? FLAG_MASK_PARITY_OVERFLOW : 0;
			z80->state->f |= (correction_factor >> (6 - FLAG_BIT_CARRY)) & FLAG_MASK_CARRY; /* Decimal carry. */

			break;
		}

		case Z80_OPCODE_CPL:
			z80->state->a = ~z80->state->a;
			z80->state->a &= 0xFF;

			z80->state->f |= FLAG_MASK_HALF_CARRY | FLAG_MASK_ADD_SUBTRACT;

			break;

		case Z80_OPCODE_SCF:
			z80->state->f &= FLAG_MASK_SIGN | FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW;
			z80->state->f |= FLAG_MASK_CARRY;
			break;

		case Z80_OPCODE_CCF:
			z80->state->f &= ~(FLAG_MASK_ADD_SUBTRACT | FLAG_MASK_HALF_CARRY);

			z80->state->f |= (z80->state->f & FLAG_MASK_CARRY) != 0 ? FLAG_MASK_HALF_CARRY : 0;
			z80->state->f ^= FLAG_MASK_CARRY;

			break;

		case Z80_OPCODE_HALT:
			/* TODO */
			UNIMPLEMENTED_Z80_INSTRUCTION("HALT");
			break;

		case Z80_OPCODE_ADD_A:
			READ_SOURCE;
			destination_value = z80->state->a;

			result_value_with_carry = destination_value + source_value;
			result_value = result_value_with_carry & 0xFF;

			z80->state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_ADC_A:
			READ_SOURCE;
			destination_value = z80->state->a;

			result_value_with_carry = destination_value + source_value + ((z80->state->f & FLAG_MASK_CARRY) != 0 ? 1 : 0);
			result_value = result_value_with_carry & 0xFF;

			z80->state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_SUB:
			READ_SOURCE;
			source_value = ~source_value;
			destination_value = z80->state->a;

			result_value_with_carry = destination_value + source_value + 1;
			result_value = result_value_with_carry & 0xFF;

			z80->state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->f ^= FLAG_MASK_HALF_CARRY;
			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_SBC_A:
			READ_SOURCE;
			source_value = ~source_value;
			destination_value = z80->state->a;

			result_value_with_carry = destination_value + source_value + ((z80->state->f & FLAG_MASK_CARRY) != 0 ? 0 : 1);
			result_value = result_value_with_carry & 0xFF;

			z80->state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->f ^= FLAG_MASK_HALF_CARRY;
			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_AND:
			READ_SOURCE;
			destination_value = z80->state->a;

			result_value = destination_value & source_value;

			z80->state->f = 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			z80->state->f |= FLAG_MASK_HALF_CARRY;
			CONDITION_PARITY;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_XOR:
			READ_SOURCE;
			destination_value = z80->state->a;

			result_value = destination_value ^ source_value;

			z80->state->f = 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_OR:
			READ_SOURCE;
			destination_value = z80->state->a;

			result_value = destination_value | source_value;

			z80->state->f = 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_CP:
			READ_SOURCE;
			source_value = ~source_value;
			destination_value = z80->state->a;

			result_value_with_carry = destination_value + source_value + 1;
			result_value = result_value_with_carry & 0xFF;

			z80->state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->f ^= FLAG_MASK_HALF_CARRY;
			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			break;

		case Z80_OPCODE_POP:
			result_value = MemoryRead16Bit(z80, callbacks, z80->state->stack_pointer);

			WRITE_DESTINATION;

			z80->state->stack_pointer += 2;
			z80->state->stack_pointer &= 0xFFFF;

			break;

		case Z80_OPCODE_RET_CONDITIONAL:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			if (!EvaluateCondition(z80->state->f, (Z80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case Z80_OPCODE_RET_UNCONDITIONAL:
		case Z80_OPCODE_RETN: /* TODO: The IFF1/IFF2 stuff. */
		case Z80_OPCODE_RETI: /* Ditto. */
			z80->state->program_counter = MemoryRead16Bit(z80, callbacks, z80->state->stack_pointer);
			z80->state->stack_pointer += 2;
			z80->state->stack_pointer &= 0xFFFF;
			break;

		case Z80_OPCODE_EXX:
			SWAP(z80->state->b, z80->state->b_);
			SWAP(z80->state->c, z80->state->c_);
			SWAP(z80->state->d, z80->state->d_);
			SWAP(z80->state->e, z80->state->e_);
			SWAP(z80->state->h, z80->state->h_);
			SWAP(z80->state->l, z80->state->l_);
			break;

		case Z80_OPCODE_LD_SP_HL:
			/* This instruction requires 2 cycles. */
			z80->state->cycles += 2;

			READ_SOURCE;

			z80->state->stack_pointer = source_value;

			break;

		case Z80_OPCODE_JP_CONDITIONAL:
			if (!EvaluateCondition(z80->state->f, (Z80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case Z80_OPCODE_JP_UNCONDITIONAL:
		case Z80_OPCODE_JP_HL:
			READ_SOURCE;

			z80->state->program_counter = source_value;

			break;

		case Z80_OPCODE_CB_PREFIX:
		case Z80_OPCODE_ED_PREFIX:
			/* Should never occur: these are handled by `DecodeInstruction`. */
			assert(0);
			break;

		case Z80_OPCODE_DD_PREFIX:
			z80->state->register_mode = Z80_REGISTER_MODE_IX;
			break;

		case Z80_OPCODE_FD_PREFIX:
			z80->state->register_mode = Z80_REGISTER_MODE_IY;
			break;

		case Z80_OPCODE_OUT:
			/* TODO */
			UNIMPLEMENTED_Z80_INSTRUCTION("OUT");
			break;

		case Z80_OPCODE_IN:
			/* TODO */
			UNIMPLEMENTED_Z80_INSTRUCTION("IN");
			break;

		case Z80_OPCODE_EX_SP_HL:
			/* This instruction requires 3 extra cycles. */
			z80->state->cycles += 3;

			READ_DESTINATION;
			result_value = MemoryRead16Bit(z80, callbacks, z80->state->stack_pointer);
			MemoryWrite16Bit(z80, callbacks, z80->state->stack_pointer, destination_value);
			WRITE_DESTINATION;
			break;

		case Z80_OPCODE_EX_DE_HL:
			SWAP(z80->state->d, z80->state->h);
			SWAP(z80->state->e, z80->state->l);
			break;

		case Z80_OPCODE_DI:
			z80->state->interrupts_enabled = cc_false;
			break;

		case Z80_OPCODE_EI:
			z80->state->interrupts_enabled = cc_true;
			break;

		case Z80_OPCODE_PUSH:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			READ_SOURCE;

			--z80->state->stack_pointer;
			z80->state->stack_pointer &= 0xFFFF;
			MemoryWrite(z80, callbacks, z80->state->stack_pointer, source_value >> 8);

			--z80->state->stack_pointer;
			z80->state->stack_pointer &= 0xFFFF;
			MemoryWrite(z80, callbacks, z80->state->stack_pointer, source_value & 0xFF);

			break;

		case Z80_OPCODE_CALL_CONDITIONAL:
			if (!EvaluateCondition(z80->state->f, (Z80_Condition)instruction->metadata->condition))
				break;
			/* Fallthrough */
		case Z80_OPCODE_CALL_UNCONDITIONAL:
			/* This instruction takes an extra cycle. */
			z80->state->cycles += 1;

			--z80->state->stack_pointer;
			z80->state->stack_pointer &= 0xFFFF;
			MemoryWrite(z80, callbacks, z80->state->stack_pointer, z80->state->program_counter >> 8);

			--z80->state->stack_pointer;
			z80->state->stack_pointer &= 0xFFFF;
			MemoryWrite(z80, callbacks, z80->state->stack_pointer, z80->state->program_counter & 0xFF);

			z80->state->program_counter = instruction->literal;
			break;

		case Z80_OPCODE_RST:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			--z80->state->stack_pointer;
			z80->state->stack_pointer &= 0xFFFF;
			MemoryWrite(z80, callbacks, z80->state->stack_pointer, z80->state->program_counter >> 8);

			--z80->state->stack_pointer;
			z80->state->stack_pointer &= 0xFFFF;
			MemoryWrite(z80, callbacks, z80->state->stack_pointer, z80->state->program_counter & 0xFF);

			z80->state->program_counter = instruction->metadata->embedded_literal;
			break;

		case Z80_OPCODE_RLC:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = (destination_value << 1) & 0xFF;
			result_value |= carry ? 0x01 : 0;

			z80->state->f = 0;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_RRC:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = destination_value >> 1;
			result_value |= carry ? 0x80 : 0;

			z80->state->f = 0;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_RL:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = (destination_value << 1) & 0xFF;
			result_value |= (z80->state->f &= FLAG_MASK_CARRY) != 0 ? 0x01 : 0;

			z80->state->f = 0;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_RR:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = destination_value >> 1;
			result_value |= (z80->state->f &= FLAG_MASK_CARRY) != 0 ? 0x80 : 0;

			z80->state->f = 0;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_SLA:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = (destination_value << 1) & 0xFF;

			z80->state->f = 0;
			z80->state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			z80->state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_SLL:
			READ_DESTINATION;

			carry = (destination_value & 0x80) != 0;

			result_value = ((destination_value << 1) | 1) & 0xFF;

			z80->state->f = 0;
			z80->state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			z80->state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_SRA:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = (destination_value >> 1) | (destination_value & 0x80);

			z80->state->f = 0;
			z80->state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			z80->state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_SRL:
			READ_DESTINATION;

			carry = (destination_value & 0x01) != 0;

			result_value = destination_value >> 1;

			z80->state->f = 0;
			z80->state->f |= (result_value & 0x80) != 0 ? FLAG_MASK_SIGN : 0;
			z80->state->f |= result_value == 0 ? FLAG_MASK_ZERO : 0;
			CONDITION_PARITY;
			z80->state->f |= carry ? FLAG_MASK_CARRY : 0;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_BIT:
			READ_DESTINATION;

			/* The setting of the parity and sign bits doesn't seem to be documented anywhere. */
			/* TODO: See if emulating this instruction with a SUB instruction produces the proper condition codes. */
			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= ((destination_value & instruction->metadata->embedded_literal) == 0) ? FLAG_MASK_ZERO | FLAG_MASK_PARITY_OVERFLOW : 0;
			z80->state->f |= FLAG_MASK_HALF_CARRY;
			z80->state->f |= instruction->metadata->embedded_literal == 0x80 && (z80->state->f & FLAG_MASK_ZERO) == 0 ? FLAG_MASK_SIGN : 0;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_RES:
			READ_DESTINATION;

			result_value = destination_value & instruction->metadata->embedded_literal;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_SET:
			READ_DESTINATION;

			result_value = destination_value | instruction->metadata->embedded_literal;

			WRITE_DESTINATION;

			/* The memory-accessing version takes an extra cycle. */
			z80->state->cycles += instruction->metadata->operands[1] == Z80_OPERAND_HL_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IX_INDIRECT
				|| instruction->metadata->operands[1] == Z80_OPERAND_IY_INDIRECT;

			break;

		case Z80_OPCODE_IN_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("IN (register)");
			break;

		case Z80_OPCODE_IN_NO_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("IN (no register)");
			break;

		case Z80_OPCODE_OUT_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("OUT (register)");
			break;

		case Z80_OPCODE_OUT_NO_REGISTER:
			UNIMPLEMENTED_Z80_INSTRUCTION("OUT (no register)");
			break;

		case Z80_OPCODE_SBC_HL:
			READ_SOURCE;
			READ_DESTINATION;

			source_value = ~(cc_u32f)source_value;

			result_value_with_carry_16bit = (cc_u32f)source_value + (cc_u32f)destination_value + ((z80->state->f & FLAG_MASK_CARRY) != 0 ? 0 : 1);;
			result_value = result_value_with_carry_16bit & 0xFFFF;

			z80->state->f = 0;

			CONDITION_SIGN_16BIT;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY_16BIT;
			CONDITION_OVERFLOW_16BIT;
			CONDITION_CARRY_16BIT;

			z80->state->f ^= FLAG_MASK_HALF_CARRY;
			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			WRITE_DESTINATION;

			/* This instruction requires an extra 7 cycles. */
			z80->state->cycles += 7;

			break;

		case Z80_OPCODE_ADC_HL:
			READ_SOURCE;
			READ_DESTINATION;

			result_value_with_carry_16bit = (cc_u32f)source_value + (cc_u32f)destination_value + ((z80->state->f & FLAG_MASK_CARRY) != 0 ? 1 : 0);
			result_value = result_value_with_carry_16bit & 0xFFFF;

			z80->state->f = 0;

			CONDITION_SIGN_16BIT;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY_16BIT;
			CONDITION_OVERFLOW_16BIT;
			CONDITION_CARRY_16BIT;

			WRITE_DESTINATION;

			/* This instruction requires an extra 7 cycles. */
			z80->state->cycles += 7;

			break;

		case Z80_OPCODE_NEG:
			source_value = z80->state->a;
			source_value = ~source_value;
			destination_value = 0;

			result_value_with_carry = destination_value + source_value + 1;
			result_value = result_value_with_carry & 0xFF;

			z80->state->f = 0;
			CONDITION_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;
			CONDITION_OVERFLOW;

			z80->state->f ^= FLAG_MASK_HALF_CARRY;
			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			z80->state->a = result_value;

			break;

		case Z80_OPCODE_IM:
			UNIMPLEMENTED_Z80_INSTRUCTION("IM");
			break;

		case Z80_OPCODE_LD_I_A:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			z80->state->i = z80->state->a;

			break;

		case Z80_OPCODE_LD_R_A:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			z80->state->r = z80->state->a;

			break;

		case Z80_OPCODE_LD_A_I:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			z80->state->a = z80->state->i;

			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= (z80->state->a >> (7 - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN;
			z80->state->f |= z80->state->a == 0 ? FLAG_MASK_ZERO : 0;
			/* TODO: IFF2 parity bit stuff. */

			break;

		case Z80_OPCODE_LD_A_R:
			/* This instruction requires an extra cycle. */
			z80->state->cycles += 1;

			z80->state->a = z80->state->r;

			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= (z80->state->a >> (7 - FLAG_BIT_SIGN)) & FLAG_MASK_SIGN;
			z80->state->f |= z80->state->a == 0 ? FLAG_MASK_ZERO : 0;
			/* TODO: IFF2 parity bit stuff. */

			break;

		case Z80_OPCODE_RRD:
		{
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;
			const cc_u8f hl_value = MemoryRead(z80, callbacks, hl);
			const cc_u8f hl_high = (hl_value >> 4) & 0xF;
			const cc_u8f hl_low = (hl_value >> 0) & 0xF;
			const cc_u8f a_high = (z80->state->a >> 4) & 0xF;
			const cc_u8f a_low = (z80->state->a >> 0) & 0xF;

			/* This instruction requires an extra 4 cycles. */
			z80->state->cycles += 4;

			MemoryWrite(z80, callbacks, hl, (a_low << 4) | (hl_high << 0));
			result_value = (a_high << 4) | (hl_low << 0);

			z80->state->f &= FLAG_MASK_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			z80->state->a = result_value;

			break;
		}

		case Z80_OPCODE_RLD:
		{
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;
			const cc_u8f hl_value = MemoryRead(z80, callbacks, hl);
			const cc_u8f hl_high = (hl_value >> 4) & 0xF;
			const cc_u8f hl_low = (hl_value >> 0) & 0xF;
			const cc_u8f a_high = (z80->state->a >> 4) & 0xF;
			const cc_u8f a_low = (z80->state->a >> 0) & 0xF;

			/* This instruction requires an extra 4 cycles. */
			z80->state->cycles += 4;

			MemoryWrite(z80, callbacks, hl, (hl_low << 4) | (a_low << 0));
			result_value = (a_high << 4) | (hl_high << 0);

			z80->state->f &= FLAG_MASK_CARRY;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_PARITY;

			z80->state->a = result_value;

			break;
		}

		case Z80_OPCODE_LDI:
		{
			const cc_u16f de = ((cc_u16f)z80->state->d << 8) | z80->state->e;
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			MemoryWrite(z80, callbacks, de, MemoryRead(z80, callbacks, hl));

			/* Increment 'hl'. */
			++z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0)
			{
				++z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Increment 'de'. */
			++z80->state->e;
			z80->state->e &= 0xFF;

			if (z80->state->e == 0)
			{
				++z80->state->d;
				z80->state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			break;
		}

		case Z80_OPCODE_LDD:
		{
			const cc_u16f de = ((cc_u16f)z80->state->d << 8) | z80->state->e;
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			MemoryWrite(z80, callbacks, de, MemoryRead(z80, callbacks, hl));

			/* Decrement 'hl'. */
			--z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0xFF)
			{
				--z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Decrement 'de'. */
			--z80->state->e;
			z80->state->e &= 0xFF;

			if (z80->state->e == 0xFF)
			{
				--z80->state->d;
				z80->state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			break;
		}

		case Z80_OPCODE_LDIR:
		{
			const cc_u16f de = ((cc_u16f)z80->state->d << 8) | z80->state->e;
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			MemoryWrite(z80, callbacks, de, MemoryRead(z80, callbacks, hl));

			/* Increment 'hl'. */
			++z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0)
			{
				++z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Increment 'de'. */
			++z80->state->e;
			z80->state->e &= 0xFF;

			if (z80->state->e == 0)
			{
				++z80->state->d;
				z80->state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			if ((z80->state->f & FLAG_MASK_PARITY_OVERFLOW) != 0)
			{
				/* An extra 5 cycles are needed here. */
				z80->state->cycles += 5;

				z80->state->program_counter -= 2;
			}

			break;
		}

		case Z80_OPCODE_LDDR:
		{
			const cc_u16f de = ((cc_u16f)z80->state->d << 8) | z80->state->e;
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			MemoryWrite(z80, callbacks, de, MemoryRead(z80, callbacks, hl));

			/* Decrement 'hl'. */
			--z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0xFF)
			{
				--z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Decrement 'de'. */
			--z80->state->e;
			z80->state->e &= 0xFF;

			if (z80->state->e == 0xFF)
			{
				--z80->state->d;
				z80->state->d &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY | FLAG_MASK_ZERO | FLAG_MASK_SIGN;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			if ((z80->state->f & FLAG_MASK_PARITY_OVERFLOW) != 0)
			{
				/* An extra 5 cycles are needed here. */
				z80->state->cycles += 5;

				z80->state->program_counter -= 2;
			}

			break;
		}

		case Z80_OPCODE_CPI:
		{
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			source_value = MemoryRead(z80, callbacks, hl);
			destination_value = z80->state->a;
			result_value = destination_value - source_value;

			/* Increment 'hl'. */
			++z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0)
			{
				++z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			break;
		}

		case Z80_OPCODE_CPD:
		{
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			source_value = MemoryRead(z80, callbacks, hl);
			destination_value = z80->state->a;
			result_value = destination_value - source_value;

			/* Decrement 'hl'. */
			--z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0xFF)
			{
				--z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			break;
		}

		case Z80_OPCODE_CPIR:
		{
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			source_value = MemoryRead(z80, callbacks, hl);
			destination_value = z80->state->a;
			result_value = destination_value - source_value;

			/* Increment 'hl'. */
			++z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0)
			{
				++z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			if ((z80->state->f & FLAG_MASK_PARITY_OVERFLOW) != 0 && (z80->state->f & FLAG_MASK_ZERO) == 0)
			{
				/* An extra 5 cycles are needed here. */
				z80->state->cycles += 5;

				z80->state->program_counter -= 2;
			}

			break;
		}

		case Z80_OPCODE_CPDR:
		{
			const cc_u16f hl = ((cc_u16f)z80->state->h << 8) | z80->state->l;

			source_value = MemoryRead(z80, callbacks, hl);
			destination_value = z80->state->a;
			result_value = destination_value - source_value;

			/* Decrement 'hl'. */
			--z80->state->l;
			z80->state->l &= 0xFF;

			if (z80->state->l == 0xFF)
			{
				--z80->state->h;
				z80->state->h &= 0xFF;
			}

			/* Decrement 'bc'. */
			--z80->state->c;
			z80->state->c &= 0xFF;

			if (z80->state->c == 0xFF)
			{
				--z80->state->b;
				z80->state->b &= 0xFF;
			}

			z80->state->f &= FLAG_MASK_CARRY;
			z80->state->f |= (z80->state->b | z80->state->c) != 0 ? FLAG_MASK_PARITY_OVERFLOW : 0;
			CONDITION_SIGN;
			CONDITION_ZERO;
			CONDITION_HALF_CARRY;

			z80->state->f |= FLAG_MASK_ADD_SUBTRACT;

			/* This instruction requires an extra 2 cycles. */
			z80->state->cycles += 2;

			if ((z80->state->f & FLAG_MASK_PARITY_OVERFLOW) != 0 && (z80->state->f & FLAG_MASK_ZERO) == 0)
			{
				/* An extra 5 cycles are needed here. */
				z80->state->cycles += 5;

				z80->state->program_counter -= 2;
			}

			break;
		}

		case Z80_OPCODE_INI:
			UNIMPLEMENTED_Z80_INSTRUCTION("INI");
			break;

		case Z80_OPCODE_IND:
			UNIMPLEMENTED_Z80_INSTRUCTION("IND");
			break;

		case Z80_OPCODE_INIR:
			UNIMPLEMENTED_Z80_INSTRUCTION("INIR");
			break;

		case Z80_OPCODE_INDR:
			UNIMPLEMENTED_Z80_INSTRUCTION("INDR");
			break;

		case Z80_OPCODE_OUTI:
			UNIMPLEMENTED_Z80_INSTRUCTION("OTDI");
			break;

		case Z80_OPCODE_OUTD:
			UNIMPLEMENTED_Z80_INSTRUCTION("OUTD");
			break;

		case Z80_OPCODE_OTIR:
			UNIMPLEMENTED_Z80_INSTRUCTION("OTIR");
			break;

		case Z80_OPCODE_OTDR:
			UNIMPLEMENTED_Z80_INSTRUCTION("OTDR");
			break;

		#undef UNIMPLEMENTED_Z80_INSTRUCTION
	}
}

void Z80_Constant_Initialise(Z80_Constant* const constant)
{
#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
	cc_u16f i;

	/* Pre-compute instruction metadata, to speed up opcode decoding. */
	for (i = 0; i < 0x100; ++i)
	{
		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_normal[Z80_REGISTER_MODE_HL][i], INSTRUCTION_MODE_NORMAL, Z80_REGISTER_MODE_HL, i);
		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_normal[Z80_REGISTER_MODE_IX][i], INSTRUCTION_MODE_NORMAL, Z80_REGISTER_MODE_IX, i);
		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_normal[Z80_REGISTER_MODE_IY][i], INSTRUCTION_MODE_NORMAL, Z80_REGISTER_MODE_IY, i);

		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_bits[Z80_REGISTER_MODE_HL][i], INSTRUCTION_MODE_BITS, Z80_REGISTER_MODE_HL, i);
		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_bits[Z80_REGISTER_MODE_IX][i], INSTRUCTION_MODE_BITS, Z80_REGISTER_MODE_IX, i);
		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_bits[Z80_REGISTER_MODE_IY][i], INSTRUCTION_MODE_BITS, Z80_REGISTER_MODE_IY, i);

		DecodeInstructionMetadata(&constant->instruction_metadata_lookup_misc[i], INSTRUCTION_MODE_MISC, Z80_REGISTER_MODE_HL, i);
	}
#endif
}

void Z80_State_Initialise(Z80_State* const state)
{
	Z80 z80;

	/* A disgusting hack. */
	z80.state = state;

	Z80_Reset(&z80);

	/* Update on the next cycle. */
	state->cycles = 1;
}

void Z80_Reset(const Z80* const z80)
{
	/* Revert back to a prefix-free mode. */
	z80->state->register_mode = Z80_REGISTER_MODE_HL;

	z80->state->program_counter = 0;

	/* The official Z80 manual claims that this happens ('z80cpu_um.pdf' page 18). */
	z80->state->interrupts_enabled = cc_false;

	z80->state->interrupt_pending = cc_false;
}

void Z80_Interrupt(const Z80* const z80, const cc_bool assert_interrupt)
{
	z80->state->interrupt_pending = assert_interrupt;
}

cc_u16f Z80_DoCycle(const Z80* const z80, const Z80_ReadAndWriteCallbacks* const callbacks)
{
	/* Process new instruction. */
	Z80Instruction instruction;

#ifndef Z80_PRECOMPUTE_INSTRUCTION_METADATA
	Z80_InstructionMetadata metadata;
	instruction.metadata = &metadata;
#endif

	z80->state->cycles = 0;

	DecodeInstruction(z80, callbacks, &instruction);

	ExecuteInstruction(z80, callbacks, &instruction);

	/* Perform interrupt after processing the instruction. */
	/* TODO: The other interrupt modes. */
	if (z80->state->interrupt_pending
		&& z80->state->interrupts_enabled
		/* Interrupts should not be able to occur directly after a prefix instruction. */
		&& instruction.metadata->opcode != Z80_OPCODE_DD_PREFIX
		&& instruction.metadata->opcode != Z80_OPCODE_FD_PREFIX
		/* Curiously, interrupts do not occur directly after 'EI' instructions either. */
		&& instruction.metadata->opcode != Z80_OPCODE_EI)
	{
		z80->state->interrupts_enabled = cc_false;
		z80->state->interrupt_pending = cc_false;

		/* TODO: Other interrupt durations. */
		z80->state->cycles += 13; /* Interrupt mode 1 duration. */

		--z80->state->stack_pointer;
		z80->state->stack_pointer &= 0xFFFF;
		callbacks->write(callbacks->user_data, z80->state->stack_pointer, z80->state->program_counter >> 8);

		--z80->state->stack_pointer;
		z80->state->stack_pointer &= 0xFFFF;
		callbacks->write(callbacks->user_data, z80->state->stack_pointer, z80->state->program_counter & 0xFF);

		z80->state->program_counter = 0x38;
	}

	return z80->state->cycles;
}
