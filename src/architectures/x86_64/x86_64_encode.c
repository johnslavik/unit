#include <unit/base.h>
#include <unit/internal/architectures.h>
#include <unit/internal/code_buffer.h>
#include <unit/internal/compile_context.h>

// Actual opcodes
enum {
    OPCODE_MOV_RM64_R64  = 0x89,
    OPCODE_MOV_R64_RM64  = 0x8B,
    OPCODE_MOV_R64_IMM64 = 0xB8,
    OPCODE_ADD_RM64_R64 = 0x01,
    OPCODE_SUB_RM64_IMM8 = 0x83,
    OPCODE_RET = 0xc3,
    OPCODE_SYSCALL_0 = 0x0f,
    OPCODE_SYSCALL_1 = 0x05,
    OPCODE_CALL_REL32 = 0xE8,
    OPCODE_JMP_REL32 = 0xE9,
    OPCODE_CMP_RM64_IMM8  = 0x83,
    OPCODE_CMP_RM64_R64 = 0x39,
    OPCODE_JCC_REL32 = 0x0F, // second byte selects condition
    OPCODE_JE_REL32 = 0x84, // jump if equal
    OPCODE_JNE_REL32 = 0x85,  // jump if not equal
    OPCODE_JL_REL32 = 0x8C,  // jump if less
    OPCODE_JGE_REL32 = 0x8D,  // jump if greater or equal
    OPCODE_JLE_REL32 = 0x8E,  // jump if less or equal
    OPCODE_JG_REL32 = 0x8F,  // jump if greater
    OPCODE_LEA = 0x8D,
};

/*
 * The "group" name comes from Intel. They call opcodes like FF and 83 "groups"
 * because the reg field of ModRM selects which instruction it actually is.
 */

// Multi-purpose opcodes (specify the actual thing using ModRM)
enum {
    OPCODE_GROUP5 = 0xFF,
    OPCODE_GROUP1_IMM8  = 0x83,
    OPCODE_GROUP1_IMM32 = 0x81,
};

// Group 1
enum {
    GROUP1_ADD = 0,
    GROUP1_OR  = 1,
    GROUP1_AND = 4,
    GROUP1_SUB = 5,
    GROUP1_XOR = 6,
    GROUP1_CMP = 7,
};

// Group 5
enum {
    GROUP5_CALL = 2,
    GROUP5_JMP  = 4,
    GROUP5_PUSH = 6,
};

enum {
    REX = 0x40,
    REX_W = 0x08,
    REX_R = 0x04,
    REX_X = 0x02,
    REX_B = 0x01,
};

enum {
    // SIB byte fields: (scale << 6) | (index << 3) | base
    // RSP as index means "no index"
    SIB_RSP_BASE = 0x24,  // base=RSP, index=none, scale=1
};

/* REX is a single byte used to access 64-bit features.
 * This is definitely not intuitive.
 *
 * W: Indicates that the operand size is 64-bit.
 * R: Used to access higher registers. When set, the reg field in ModRM starts
 * at r8 instead of r1.
 * X: Just leave this at zero for now. This is apparently useful for something
 * called "SIB".
 * B: Also used to access higher registers. When set, the rm field in ModRM
 * starts at r8.
 */
static uint8_t
rex(uint8_t w, uint8_t r, uint8_t x, uint8_t b) {
    return
        REX |
        (w ? REX_W : 0) |
        (r ? REX_R : 0) |
        (x ? REX_X : 0) |
        (b ? REX_B : 0);
}

typedef enum {
    MOD_INDIRECT = 0x0, // register is a memory Address
    MOD_INDIRECT_DISP8 = 0x1, // memory + 8-bit offset
    MOD_INDIRECT_DISP32 = 0x2, // memory + 32-bit offset
    MOD_REGISTER = 0x3, // register directly
} ModRM_Mode;

/* ModRM is a single byte after the opcode that tells the CPU how to
 * interpret the operands.
 *
 * mod: The mode.
 * reg: Either a register or an opcode extension.
 * rm: The other register or the base for a memory Address.
 */
static uint8_t
modrm(ModRM_Mode mode, uint8_t reg, uint8_t rm) {
    return
        (mode << 6) |
        (reg << 3) |
        rm;
}

static UNIT_Status
add_jump(_UNIT_CompileContext *compile_context, UNIT_Size label_index)
{
    assert(compile_context != NULL);
    _UNIT_PendingJump *jump = _UNIT_PendingJump_New(
        compile_context->context,
        _UNIT_CodeBuffer_CurrentIndex(&compile_context->buffer),
        label_index
    );
    if (jump == NULL) {
        return UNIT_FAIL;
    }

    if (UNIT_FAILED(_UNIT_Vector_Append(&compile_context->jump_table.pending_jumps,
                                        jump))) {
        return UNIT_FAIL;
    }

    return UNIT_OK;
}

UNIT_Status
_UNIT_X86_64_encode_instruction(_UNIT_CompileContext *compile_context,
                                _UNIT_X86_64_Instruction *instr)
{
    assert(compile_context != NULL);
    assert(instr != NULL);
    switch (instr->opcode) {

#define EMIT8(value)                                                                \
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit8(&compile_context->buffer, value))) {     \
        goto error;                                                                 \
    }
#define EMIT32(value)                                                               \
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit32(&compile_context->buffer, value))) {    \
        goto error;                                                                 \
    }
#define EMIT64(value)                                                               \
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit64(&compile_context->buffer, value))) {    \
        goto error;                                                                 \
    }
#define INDEX() _UNIT_CodeBuffer_CurrentIndex(&compile_context->buffer)
#define EMIT_JUMP(label_index)                                      \
    if (UNIT_FAILED(add_jump(compile_context, label_index))) {      \
        goto error;                                                 \
    }                                                               \
    EMIT32(0x00);

        case X86_64_RET: {
            EMIT8(OPCODE_RET);
            break;
        }

        case X86_64_SYSCALL: {
            EMIT8(OPCODE_SYSCALL_0);
            EMIT8(OPCODE_SYSCALL_1);
            break;
        }

        case X86_64_MOV: {
            _UNIT_X86_64_Operand dst = instr->operands[0];
            _UNIT_X86_64_Operand src = instr->operands[1];

            // mov reg, imm64
            if (dst.kind == OPERAND_REGISTER && src.kind == OPERAND_IMMEDIATE) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_MOV_R64_IMM64 + dst.reg);
                EMIT64(src.immediate);
            }
            // mov reg, reg
            else if (dst.kind == OPERAND_REGISTER && src.kind == OPERAND_REGISTER) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_MOV_RM64_R64);
                EMIT8(modrm(MOD_REGISTER, src.reg, dst.reg));
            }
            // mov [rsp + offset], reg
            else if (dst.kind == OPERAND_STACK && src.kind == OPERAND_REGISTER) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_MOV_RM64_R64);
                if (dst.immediate == 0) {
                    EMIT8(modrm(MOD_INDIRECT, src.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                } else if (dst.immediate <= 127) {
                    EMIT8(modrm(MOD_INDIRECT_DISP8, src.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                    EMIT8((uint8_t)dst.immediate);
                } else {
                    EMIT8(modrm(MOD_INDIRECT_DISP32, src.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                    EMIT32((uint32_t)dst.immediate);
                }
            }
            // mov reg, [rsp + offset]
            else if (dst.kind == OPERAND_REGISTER && src.kind == OPERAND_STACK) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_MOV_R64_RM64);
                if (src.immediate == 0) {
                    EMIT8(modrm(MOD_INDIRECT, dst.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                } else if (src.immediate <= 127) {
                    EMIT8(modrm(MOD_INDIRECT_DISP8, dst.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                    EMIT8((uint8_t)src.immediate);
                } else {
                    EMIT8(modrm(MOD_INDIRECT_DISP32, dst.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                    EMIT32((uint32_t)src.immediate);
                }
            }
            break;
        }

        case X86_64_ADD: {
            _UNIT_X86_64_Operand dst = instr->operands[0];
            _UNIT_X86_64_Operand src = instr->operands[1];

            // add reg, reg
            if (dst.kind == OPERAND_REGISTER && src.kind == OPERAND_REGISTER) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_ADD_RM64_R64);
                EMIT8(modrm(MOD_REGISTER, src.reg, dst.reg));
            }
            // add reg, imm32
            else {
                assert(src.kind == OPERAND_IMMEDIATE);
                assert(dst.kind == OPERAND_REGISTER);
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_GROUP1_IMM32);
                EMIT8(modrm(MOD_REGISTER, GROUP1_ADD, dst.reg));
                EMIT32((uint32_t)src.immediate);
            }
            break;
        }

        case X86_64_CALL_INDIRECT: {
            _UNIT_X86_64_Operand target = instr->operands[0];
            assert(target.kind == OPERAND_REGISTER);
            EMIT8(rex(1, 0, 0, 0));
            EMIT8(OPCODE_GROUP5);
            EMIT8(modrm(MOD_REGISTER, GROUP5_CALL, target.reg));
            break;
        }

        case X86_64_CALL_SYMBOL: {
            EMIT8(OPCODE_CALL_REL32);
            // Record the offset where the linker needs to patch
            // (this is where the 4-byte displacement starts)
            _UNIT_Relocation *relocation = _UNIT_Relocation_NewCall(
                compile_context->context,
                INDEX(),
                instr->operands[0].immediate
            );
            if (relocation == NULL) {
                goto error;
            }
            if (UNIT_FAILED(_UNIT_Vector_Append(&compile_context->symbol_table.relocations,
                                                relocation))) {
                goto error;
            }
            EMIT32(0x00);
            break;
        }

        case X86_64_JUMP: {
            UNIT_Size label_index = instr->operands[0].immediate;
            EMIT8(OPCODE_JMP_REL32);
            EMIT_JUMP(label_index);
            break;
        }

        case X86_64_JUMP_LABEL: {
            UNIT_Size label_index = instr->operands[0].immediate;
            if (UNIT_FAILED(_UNIT_SizeMap_Set(&compile_context->jump_table.label_offsets,
                                            label_index,
                                            INDEX()))) {
                goto error;
            }
            // Labels are just markers; don't emit anything.
            break;
        }

        case X86_64_COMPARE: {
            _UNIT_X86_64_Operand dst = instr->operands[0];
            _UNIT_X86_64_Operand src = instr->operands[1];
            // cmp reg, imm
            if (dst.kind == OPERAND_REGISTER && src.kind == OPERAND_IMMEDIATE) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_CMP_RM64_IMM8);
                EMIT8(modrm(MOD_REGISTER, GROUP1_CMP, dst.reg));
                EMIT8((uint8_t)src.immediate);
            }
            // cmp reg, reg
            else {
                assert(src.kind == OPERAND_REGISTER);
                assert(dst.kind == OPERAND_REGISTER);
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_CMP_RM64_R64);
                EMIT8(modrm(MOD_REGISTER, src.reg, dst.reg));
            }

            break;
        }

#define CONDITIONAL_JUMP_CASE(name, condition_opcode)           \
    case name: {                                                \
        UNIT_Size label_index = instr->operands[0].immediate;   \
        EMIT8(OPCODE_JCC_REL32);                                \
        EMIT8(condition_opcode);                                \
        EMIT_JUMP(label_index);                                 \
        break;                                                  \
    }

        CONDITIONAL_JUMP_CASE(X86_64_JUMP_IF_EQUAL, OPCODE_JE_REL32)
        CONDITIONAL_JUMP_CASE(X86_64_JUMP_IF_NOT_EQUAL, OPCODE_JNE_REL32)
        CONDITIONAL_JUMP_CASE(X86_64_JUMP_IF_LESS, OPCODE_JL_REL32)
        CONDITIONAL_JUMP_CASE(X86_64_JUMP_IF_GREATER, OPCODE_JG_REL32)
        CONDITIONAL_JUMP_CASE(X86_64_JUMP_IF_LESS_EQUAL, OPCODE_JLE_REL32)
        CONDITIONAL_JUMP_CASE(X86_64_JUMP_IF_GREATER_EQUAL, OPCODE_JGE_REL32)

#undef CONDITIONAL_JUMP_CASE

        case X86_64_LOAD_STRING: {
            _UNIT_X86_64_Operand dst = instr->operands[0];
            UNIT_Size string_index = instr->operands[1].immediate;
            _UNIT_SizeMap *string_offsets = &compile_context->string_data.string_offsets;
            UNIT_Size byte_offset = _UNIT_SizeMap_GET(string_offsets, string_index);

            EMIT8(rex(1, 0, 0, 0));
            EMIT8(OPCODE_LEA);
            EMIT8(modrm(MOD_INDIRECT, dst.reg, 5));

            _UNIT_Relocation *relocation = _UNIT_Relocation_NewData(compile_context->context, INDEX(),
                                                                    byte_offset);
            if (relocation == NULL) {
                goto error;
            }
            if (UNIT_FAILED(_UNIT_Vector_Append(&compile_context->symbol_table.relocations,
                                                relocation))) {
                goto error;
            }
            EMIT32(0x00);
            break;
        }

        case X86_64_LOAD_ADDRESS: {
            _UNIT_X86_64_Operand dst = instr->operands[0];
            _UNIT_X86_64_Operand src = instr->operands[1];

            // lea reg, [rsp + offset]
            if (dst.kind == OPERAND_REGISTER && src.kind == OPERAND_STACK) {
                EMIT8(rex(1, 0, 0, 0));
                EMIT8(OPCODE_LEA);
                if (src.immediate == 0) {
                    EMIT8(modrm(MOD_INDIRECT, dst.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                } else if (src.immediate <= 127) {
                    EMIT8(modrm(MOD_INDIRECT_DISP8, dst.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                    EMIT8((uint8_t)src.immediate);
                } else {
                    EMIT8(modrm(MOD_INDIRECT_DISP32, dst.reg, REG_RSP));
                    EMIT8(SIB_RSP_BASE);
                    EMIT32((uint32_t)src.immediate);
                }
            }
            break;
        }

        default:
            _UNIT_Unreachable();
    }

    _UNIT_Dealloc(compile_context->context, instr);
    return UNIT_OK;
error:
    _UNIT_Dealloc(compile_context->context, instr);
    return UNIT_FAIL;
}

void
_UNIT_X86_64_PatchPrologue(_UNIT_CompileContext *context,
                        UNIT_Size prologue_offset)
{
    if (context->frame_size > 0) {
        // Align to 16 bytes
        if (context->frame_size % 16 != 0) {
            context->frame_size += 16 - (context->frame_size % 16);
        }

        uint8_t prologue[] = {
            rex(1, 0, 0, 0),
            OPCODE_GROUP1_IMM32,
            modrm(MOD_REGISTER, GROUP1_SUB, REG_RSP),
            (context->frame_size >> 0) & 0xFF,
            (context->frame_size >> 8) & 0xFF,
            (context->frame_size >> 16) & 0xFF,
            (context->frame_size >> 24) & 0xFF,
        };
        _UNIT_CodeBuffer_PatchBytes(&context->buffer, prologue_offset,
                                    prologue, 7);
    } else {
        uint8_t nops[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        _UNIT_CodeBuffer_PatchBytes(&context->buffer, prologue_offset,
                                    nops, 7);
    }
}

void
_UNIT_X86_64_PatchJumps(_UNIT_CompileContext *context)
{
    assert(context != NULL);
    UNIT_Size count = _UNIT_Vector_SIZE(&context->jump_table.pending_jumps);

    for (UNIT_Size index = 0; index < count; ++index) {
        _UNIT_PendingJump *jump = _UNIT_Vector_GET(&context->jump_table.pending_jumps,
                                                   index);
        UNIT_Size label_offset = _UNIT_SizeMap_GET(&context->jump_table.label_offsets,
                                                    jump->label_index);

        // Displacement is relative to the end of the jmp instruction.
        // The end is 4 bytes after patch_offset (the rel32 field).
        UNIT_Size instruction_end = jump->patch_offset + 4;
        int32_t displacement = (int32_t)(label_offset - instruction_end);
        _UNIT_CodeBuffer_Patch32(&context->buffer, jump->patch_offset,
                                 displacement);
    }
}
