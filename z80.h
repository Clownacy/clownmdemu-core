#ifndef Z80_H
#define Z80_H

/* If enabled, a lookup table is used to optimise opcode decoding. Disable this to save RAM. */
#define Z80_PRECOMPUTE_INSTRUCTION_METADATA

#include "clowncommon/clowncommon.h"

typedef enum Z80_Opcode
{
	Z80_OPCODE_NOP,
	Z80_OPCODE_EX_AF_AF,
	Z80_OPCODE_DJNZ,
	Z80_OPCODE_JR_UNCONDITIONAL,
	Z80_OPCODE_JR_CONDITIONAL,
	Z80_OPCODE_LD_16BIT,
	Z80_OPCODE_ADD_HL,
	Z80_OPCODE_LD_8BIT,
	Z80_OPCODE_INC_16BIT,
	Z80_OPCODE_DEC_16BIT,
	Z80_OPCODE_INC_8BIT,
	Z80_OPCODE_DEC_8BIT,
	Z80_OPCODE_RLCA,
	Z80_OPCODE_RRCA,
	Z80_OPCODE_RLA,
	Z80_OPCODE_RRA,
	Z80_OPCODE_DAA,
	Z80_OPCODE_CPL,
	Z80_OPCODE_SCF,
	Z80_OPCODE_CCF,
	Z80_OPCODE_HALT,
	Z80_OPCODE_ADD_A,
	Z80_OPCODE_ADC_A,
	Z80_OPCODE_SUB,
	Z80_OPCODE_SBC_A,
	Z80_OPCODE_AND,
	Z80_OPCODE_XOR,
	Z80_OPCODE_OR,
	Z80_OPCODE_CP,
	Z80_OPCODE_RET_CONDITIONAL,
	Z80_OPCODE_POP,
	Z80_OPCODE_RET_UNCONDITIONAL,
	Z80_OPCODE_EXX,
	Z80_OPCODE_JP_HL,
	Z80_OPCODE_LD_SP_HL,
	Z80_OPCODE_JP_CONDITIONAL,
	Z80_OPCODE_JP_UNCONDITIONAL,
	Z80_OPCODE_CB_PREFIX,
	Z80_OPCODE_OUT,
	Z80_OPCODE_IN,
	Z80_OPCODE_EX_SP_HL,
	Z80_OPCODE_EX_DE_HL,
	Z80_OPCODE_DI,
	Z80_OPCODE_EI,
	Z80_OPCODE_CALL_CONDITIONAL,
	Z80_OPCODE_PUSH,
	Z80_OPCODE_CALL_UNCONDITIONAL,
	Z80_OPCODE_DD_PREFIX,
	Z80_OPCODE_ED_PREFIX,
	Z80_OPCODE_FD_PREFIX,
	Z80_OPCODE_RST,
	Z80_OPCODE_RLC,
	Z80_OPCODE_RRC,
	Z80_OPCODE_RL,
	Z80_OPCODE_RR,
	Z80_OPCODE_SLA,
	Z80_OPCODE_SRA,
	Z80_OPCODE_SLL,
	Z80_OPCODE_SRL,
	Z80_OPCODE_BIT,
	Z80_OPCODE_RES,
	Z80_OPCODE_SET,
	Z80_OPCODE_IN_REGISTER,
	Z80_OPCODE_IN_NO_REGISTER,
	Z80_OPCODE_OUT_REGISTER,
	Z80_OPCODE_OUT_NO_REGISTER,
	Z80_OPCODE_SBC_HL,
	Z80_OPCODE_ADC_HL,
	Z80_OPCODE_NEG,
	Z80_OPCODE_RETN,
	Z80_OPCODE_RETI,
	Z80_OPCODE_IM,
	Z80_OPCODE_LD_I_A,
	Z80_OPCODE_LD_R_A,
	Z80_OPCODE_LD_A_I,
	Z80_OPCODE_LD_A_R,
	Z80_OPCODE_RRD,
	Z80_OPCODE_RLD,
	Z80_OPCODE_LDI,
	Z80_OPCODE_LDD,
	Z80_OPCODE_LDIR,
	Z80_OPCODE_LDDR,
	Z80_OPCODE_CPI,
	Z80_OPCODE_CPD,
	Z80_OPCODE_CPIR,
	Z80_OPCODE_CPDR,
	Z80_OPCODE_INI,
	Z80_OPCODE_IND,
	Z80_OPCODE_INIR,
	Z80_OPCODE_INDR,
	Z80_OPCODE_OUTI,
	Z80_OPCODE_OUTD,
	Z80_OPCODE_OTIR,
	Z80_OPCODE_OTDR
} Z80_Opcode;

typedef enum Z80_Operand
{
	Z80_OPERAND_NONE,
	Z80_OPERAND_A,
	Z80_OPERAND_B,
	Z80_OPERAND_C,
	Z80_OPERAND_D,
	Z80_OPERAND_E,
	Z80_OPERAND_H,
	Z80_OPERAND_L,
	Z80_OPERAND_IXH,
	Z80_OPERAND_IXL,
	Z80_OPERAND_IYH,
	Z80_OPERAND_IYL,
	Z80_OPERAND_AF,
	Z80_OPERAND_BC,
	Z80_OPERAND_DE,
	Z80_OPERAND_HL,
	Z80_OPERAND_IX,
	Z80_OPERAND_IY,
	Z80_OPERAND_PC,
	Z80_OPERAND_SP,
	Z80_OPERAND_BC_INDIRECT,
	Z80_OPERAND_DE_INDIRECT,
	Z80_OPERAND_HL_INDIRECT,
	Z80_OPERAND_IX_INDIRECT,
	Z80_OPERAND_IY_INDIRECT,
	Z80_OPERAND_ADDRESS,
	Z80_OPERAND_LITERAL_8BIT,
	Z80_OPERAND_LITERAL_16BIT
} Z80_Operand;

typedef enum Z80_Condition
{
	Z80_CONDITION_NOT_ZERO = 0,
	Z80_CONDITION_ZERO = 1,
	Z80_CONDITION_NOT_CARRY = 2,
	Z80_CONDITION_CARRY = 3,
	Z80_CONDITION_PARITY_OVERFLOW = 4,
	Z80_CONDITION_PARITY_EQUALITY = 5,
	Z80_CONDITION_PLUS = 6,
	Z80_CONDITION_MINUS = 7
} Z80_Condition;

typedef enum Z80_RegisterMode
{
	Z80_REGISTER_MODE_HL,
	Z80_REGISTER_MODE_IX,
	Z80_REGISTER_MODE_IY
} Z80_RegisterMode;

typedef struct Z80_InstructionMetadata
{
	/* These three are actually enums packed into chars to save RAM. */
	cc_u8l opcode;      /* Z80_Opcode */
	cc_u8l operands[2]; /* Z80_Operand */
	cc_u8l condition;   /* Z80_Condition */
	cc_u8l embedded_literal;
	cc_bool has_displacement;
} Z80_InstructionMetadata;

typedef struct Z80_Constant
{
#ifdef Z80_PRECOMPUTE_INSTRUCTION_METADATA
	Z80_InstructionMetadata instruction_metadata_lookup_normal[3][0x100];
	Z80_InstructionMetadata instruction_metadata_lookup_bits[3][0x100];
	Z80_InstructionMetadata instruction_metadata_lookup_misc[0x100];
#endif
} Z80_Constant;

typedef struct Z80_State
{
	cc_u8l register_mode; /* Z80_RegisterMode */
	cc_u16l cycles;
	cc_u16l program_counter;
	cc_u16l stack_pointer;
	cc_u8l a, f, b, c, d, e, h, l;
	cc_u8l a_, f_, b_, c_, d_, e_, h_, l_; /* Backup registers. */
	cc_u8l ixh, ixl, iyh, iyl;
	cc_u8l r, i;
	cc_bool interrupts_enabled;
	cc_bool interrupt_pending;
} Z80_State;

typedef struct Z80_ReadAndWriteCallbacks
{
	cc_u16f (*read)(const void *user_data, cc_u16f address);
	void (*write)(const void *user_data, cc_u16f address, cc_u16f value);
	const void *user_data;
} Z80_ReadAndWriteCallbacks;

typedef struct Z80
{
	const Z80_Constant *constant;
	Z80_State *state;
} Z80;

void Z80_Constant_Initialise(Z80_Constant *constant);
void Z80_State_Initialise(Z80_State *state);
void Z80_Reset(const Z80 *z80);
void Z80_Interrupt(const Z80 *z80, cc_bool assert_interrupt);
cc_u16f Z80_DoCycle(const Z80 *z80, const Z80_ReadAndWriteCallbacks *callbacks);

#endif /* Z80_H */
