#ifndef UNIT_AARCH64_LOCAL_H
#define UNIT_AARCH64_LOCAL_H

#include <unit/base.h>

#include <unit/internal/compile_context.h>

typedef enum {
    REG_X0  = 0,
    REG_X1  = 1,
    REG_X2  = 2,
    REG_X3  = 3,
    REG_X4  = 4,
    REG_X5  = 5,
    REG_X6  = 6,
    REG_X7  = 7,
    REG_X8  = 8,
    REG_X9  = 9,
    REG_X10 = 10,
    REG_X11 = 11,
    REG_X12 = 12,
    REG_X13 = 13,
    REG_X14 = 14,
    REG_X15 = 15,
    REG_X16 = 16,  // IP0 (scratch)
    REG_X17 = 17,  // IP1 (scratch)
    REG_X18 = 18,  // Platform register (reserved on Apple)
    REG_X19 = 19,
    REG_X20 = 20,
    REG_X21 = 21,
    REG_X22 = 22,
    REG_X23 = 23,
    REG_X24 = 24,
    REG_X25 = 25,
    REG_X26 = 26,
    REG_X27 = 27,
    REG_X28 = 28,
    REG_FP  = 29,  // Frame pointer
    REG_LR  = 30,  // Link register
    REG_SP  = 31,  // Stack pointer (in some encodings) / XZR (in others)
    REG_XZR = 31,  // Zero register
} AArch64_Register;

typedef enum {
    // Moves
    AARCH64_MOV_REG,
    AARCH64_MOV_IMM,
    AARCH64_MOVK,

    // Loads / Stores
    AARCH64_LDR,          // ldr Xt, [Xn, #imm]
    AARCH64_STR,          // str Xt, [Xn, #imm]
    AARCH64_LDRB,         // ldrb Wt, [Xn]
    AARCH64_LDRH,         // ldrh Wt, [Xn]
    AARCH64_LDRSW,        // ldrsw Xt, [Xn]
    AARCH64_LDR_W,        // ldr Wt, [Xn]
    AARCH64_STRB,         // strb Wt, [Xn]
    AARCH64_STRH,         // strh Wt, [Xn]
    AARCH64_STR_W,        // str Wt, [Xn]
    AARCH64_LDR_LITERAL,  // ldr Xt, literal (PC-relative)

    // Arithmetic
    AARCH64_ADD,
    AARCH64_ADD_IMM,
    AARCH64_SUB,
    AARCH64_SUB_IMM,
    AARCH64_MUL,
    AARCH64_SDIV,
    AARCH64_MSUB,   // msub Xd, Xn, Xm, Xa => Xa - Xn*Xm (for modulo)

    // Compare
    AARCH64_CMP,

    // Branches
    AARCH64_B,
    AARCH64_B_COND,
    AARCH64_BL,
    AARCH64_BLR,
    AARCH64_RET,

    // Labels
    AARCH64_LABEL,

    // Misc
    AARCH64_ADR,   // adr Xd, label (PC-relative)
    AARCH64_ADRP,  // adrp Xd, label (PC-relative, page)
    AARCH64_STP,   // stp Xt1, Xt2, [Xn, #imm]!
    AARCH64_LDP,   // ldp Xt1, Xt2, [Xn], #imm

    // Sign/zero extend
    AARCH64_SXTB,
    AARCH64_SXTH,
    AARCH64_SXTW,
    AARCH64_UXTB,
    AARCH64_UXTH,
    AARCH64_MOV_W,  // mov Wd, Wn (32-bit move, zero extends)

    // Call symbol (relocation-based)
    AARCH64_CALL_SYMBOL,
    AARCH64_LOAD_STRING,
} AArch64_Opcode;

typedef enum {
    A64_COND_EQ = 0x0,
    A64_COND_NE = 0x1,
    A64_COND_LT = 0xB,
    A64_COND_GE = 0xA,
    A64_COND_LE = 0xD,
    A64_COND_GT = 0xC,
} AArch64_Condition;

typedef enum {
    A64_OPERAND_REGISTER,
    A64_OPERAND_IMMEDIATE,
    A64_OPERAND_STACK,
    A64_OPERAND_INDIRECT,
    A64_OPERAND_CONDITION,
} AArch64_OperandKind;

typedef struct {
    AArch64_OperandKind kind;
    union {
        AArch64_Register reg;
        int64_t immediate;
        AArch64_Condition condition;
    };
} AArch64_Operand;

typedef struct {
    AArch64_Opcode opcode;
    AArch64_Operand operands[4];
    UNIT_Size operand_count;
} AArch64_Instruction;

UNIT_Status
AArch64_encode_instruction(_UNIT_CompileContext *context,
                           AArch64_Instruction *instr);

void
AArch64_PatchPrologue(_UNIT_CompileContext *context,
                      UNIT_Size prologue_offset,
                      UNIT_Size frame_size);

void
AArch64_PatchEpilogue(_UNIT_CompileContext *compile_context,
                      UNIT_Size epilogue_offset,
                      UNIT_Size frame_size);

void
AArch64_PatchJumps(_UNIT_CompileContext *context);

#endif