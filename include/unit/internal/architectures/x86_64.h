#ifndef UNIT_X86_64_H
#define UNIT_X86_64_H

#include <stdint.h>

#include <unit/base.h>
#include <unit/internal/code_buffer.h>
#include <unit/internal/compile_context.h>
#include <unit/internal/translation.h>

typedef enum {
    REG_RAX = 0,
    REG_RCX = 1,
    REG_RDX = 2,
    REG_RBX = 3,
    REG_RSP = 4,
    REG_RBP = 5,
    REG_RSI = 6,
    REG_RDI = 7,
    REG_R8  = 8,
    REG_R9  = 9,
    REG_R10 = 10,
    REG_R11 = 11,
    REG_R12 = 12,
    REG_R13 = 13,
    REG_R14 = 14,
    REG_R15 = 15,
} _UNIT_X86_64_Register;

typedef enum {
    X86_64_MOV,
    X86_64_ADD,
    X86_64_SUB,
    X86_64_SYSCALL,
    X86_64_RET,
    X86_64_CALL_INDIRECT,
    X86_64_CALL_SYMBOL,
    X86_64_JUMP,
    X86_64_JUMP_LABEL,
    X86_64_JUMP_IF_EQUAL,
    X86_64_JUMP_IF_NOT_EQUAL,
    X86_64_JUMP_IF_LESS,
    X86_64_JUMP_IF_GREATER,
    X86_64_JUMP_IF_LESS_EQUAL,
    X86_64_JUMP_IF_GREATER_EQUAL,
    X86_64_COMPARE,
    X86_64_LOAD_STRING,
    X86_64_LOAD_ADDRESS,
} _UNIT_X86_64_Opcode;

typedef enum {
    OPERAND_REGISTER,
    OPERAND_IMMEDIATE,
    OPERAND_MEMORY,
    OPERAND_STACK
} _UNIT_X86_64_OperandKind;

typedef struct {
    _UNIT_X86_64_OperandKind kind;

    union {
        _UNIT_X86_64_Register reg;
        uint64_t immediate;
    };
} _UNIT_X86_64_Operand;

typedef struct {
    _UNIT_X86_64_Opcode opcode;
    _UNIT_X86_64_Operand operands[2];
    UNIT_Size operand_count;
} _UNIT_X86_64_Instruction;

UNIT_Status
_UNIT_X86_64_encode_instruction(_UNIT_CompileContext *context,
                             _UNIT_X86_64_Instruction *instr);

UNIT_Status
_UNIT_X86_64_FromTranslation(_UNIT_Translation *translation,
                          _UNIT_CompileContext *context);

void
_UNIT_X86_64_PatchJumps(_UNIT_CompileContext *context);

void
_UNIT_X86_64_PatchPrologue(_UNIT_CompileContext *context,
                        UNIT_Size prologue_offset);

#endif
