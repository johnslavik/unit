#include <stdio.h>
#include <stdlib.h>

#include <unit/platform.h>

#include <unit/internal/architectures.h>
#include <unit/internal/basic_block.h>
#include <unit/internal/compile_context.h>
#include <unit/internal/size_vector.h>
#include <unit/internal/translation.h>

#include "aarch64_local.h"

// ================================================================
// Operand helpers
// ================================================================

static AArch64_Operand
a64_reg(AArch64_Register r) {
    return (AArch64_Operand) { .kind = A64_OPERAND_REGISTER, .reg = r };
}

static AArch64_Operand
a64_imm(int64_t value) {
    return (AArch64_Operand) { .kind = A64_OPERAND_IMMEDIATE, .immediate = value };
}

static AArch64_Operand
a64_stack(int64_t offset) {
    return (AArch64_Operand) { .kind = A64_OPERAND_STACK, .immediate = offset };
}

static AArch64_Operand
a64_indirect(AArch64_Register r) {
    return (AArch64_Operand) { .kind = A64_OPERAND_INDIRECT, .reg = r };
}

static AArch64_Operand
a64_cond(AArch64_Condition c) {
    return (AArch64_Operand) { .kind = A64_OPERAND_CONDITION, .condition = c };
}

// ================================================================
// Register mapping
// ================================================================

// Map virtual register indices to physical registers.
// We use X9-X15 and X19 as allocatable registers (8 total, same as AMD64).
// X16/X17 are scratch (IP0/IP1).
// X0-X7 are argument/result registers (caller-saved).
// X19-X28 are callee-saved.
static const AArch64_Register register_map[] = {
    REG_X9,
    REG_X10,
    REG_X11,
    REG_X12,
    REG_X13,
    REG_X14,
    REG_X15,
    REG_X19,
};

// Scratch register used for temporaries (IP0)
#define SCRATCH_REG REG_X16

// On AArch64, both System V (Linux) and Apple use X0-X7 for arguments.
static const AArch64_Register argument_registers[] = {
    REG_X0, REG_X1, REG_X2, REG_X3, REG_X4, REG_X5, REG_X6, REG_X7
};

// ================================================================
// Instruction builders
// ================================================================

#define BUILDER_0(name, opcode_name)                                              \
    static inline AArch64_Instruction *                                           \
    name(UNIT_Context *context) {                                                 \
        AArch64_Instruction *instruction = _UNIT_Alloc(context,                   \
                                                       sizeof(AArch64_Instruction)); \
        if (instruction == NULL) return NULL;                                     \
        instruction->opcode = opcode_name;                                        \
        instruction->operand_count = 0;                                           \
        return instruction;                                                       \
    }

#define BUILDER_1(name, opcode_name)                                              \
    static inline AArch64_Instruction *                                           \
    name(UNIT_Context *context, AArch64_Operand a) {                              \
        AArch64_Instruction *instruction = _UNIT_Alloc(context,                   \
                                                       sizeof(AArch64_Instruction)); \
        if (instruction == NULL) return NULL;                                     \
        instruction->opcode = opcode_name;                                        \
        instruction->operands[0] = a;                                             \
        instruction->operand_count = 1;                                           \
        return instruction;                                                       \
    }

#define BUILDER_2(name, opcode_name)                                              \
    static inline AArch64_Instruction *                                           \
    name(UNIT_Context *context, AArch64_Operand a, AArch64_Operand b) {           \
        AArch64_Instruction *instruction = _UNIT_Alloc(context,                   \
                                                       sizeof(AArch64_Instruction)); \
        if (instruction == NULL) return NULL;                                     \
        instruction->opcode = opcode_name;                                        \
        instruction->operands[0] = a;                                             \
        instruction->operands[1] = b;                                             \
        instruction->operand_count = 2;                                           \
        return instruction;                                                       \
    }

#define BUILDER_3(name, opcode_name)                                              \
    static inline AArch64_Instruction *                                           \
    name(UNIT_Context *context, AArch64_Operand a, AArch64_Operand b,             \
         AArch64_Operand c) {                                                     \
        AArch64_Instruction *instruction = _UNIT_Alloc(context,                   \
                                                       sizeof(AArch64_Instruction)); \
        if (instruction == NULL) return NULL;                                     \
        instruction->opcode = opcode_name;                                        \
        instruction->operands[0] = a;                                             \
        instruction->operands[1] = b;                                             \
        instruction->operands[2] = c;                                             \
        instruction->operand_count = 3;                                           \
        return instruction;                                                       \
    }

#define BUILDER_4(name, opcode_name)                                              \
    static inline AArch64_Instruction *                                           \
    name(UNIT_Context *context, AArch64_Operand a, AArch64_Operand b,             \
         AArch64_Operand c, AArch64_Operand d) {                                  \
        AArch64_Instruction *instruction = _UNIT_Alloc(context,                   \
                                                       sizeof(AArch64_Instruction)); \
        if (instruction == NULL) return NULL;                                     \
        instruction->opcode = opcode_name;                                        \
        instruction->operands[0] = a;                                             \
        instruction->operands[1] = b;                                             \
        instruction->operands[2] = c;                                             \
        instruction->operands[3] = d;                                             \
        instruction->operand_count = 4;                                           \
        return instruction;                                                       \
    }

BUILDER_0(a64_ret, AARCH64_RET)
BUILDER_1(a64_blr, AARCH64_BLR)
BUILDER_1(a64_b, AARCH64_B)
BUILDER_1(a64_label, AARCH64_LABEL)
BUILDER_1(a64_call_symbol, AARCH64_CALL_SYMBOL)
BUILDER_2(a64_mov_reg, AARCH64_MOV_REG)
BUILDER_2(a64_mov_imm, AARCH64_MOV_IMM)
BUILDER_2(a64_cmp, AARCH64_CMP)
BUILDER_2(a64_sxtb, AARCH64_SXTB)
BUILDER_2(a64_sxth, AARCH64_SXTH)
BUILDER_2(a64_sxtw, AARCH64_SXTW)
BUILDER_2(a64_uxtb, AARCH64_UXTB)
BUILDER_2(a64_uxth, AARCH64_UXTH)
BUILDER_2(a64_mov_w, AARCH64_MOV_W)
BUILDER_2(a64_ldrb, AARCH64_LDRB)
BUILDER_2(a64_ldrh, AARCH64_LDRH)
BUILDER_2(a64_ldr_w, AARCH64_LDR_W)
BUILDER_2(a64_ldrsw, AARCH64_LDRSW)
BUILDER_2(a64_strb, AARCH64_STRB)
BUILDER_2(a64_strh, AARCH64_STRH)
BUILDER_2(a64_str_w, AARCH64_STR_W)
BUILDER_2(a64_b_cond, AARCH64_B_COND)
BUILDER_2(a64_load_string, AARCH64_LOAD_STRING)
BUILDER_3(a64_ldr, AARCH64_LDR)
BUILDER_3(a64_str, AARCH64_STR)
BUILDER_3(a64_add, AARCH64_ADD)
BUILDER_3(a64_add_imm, AARCH64_ADD_IMM)
BUILDER_3(a64_sub, AARCH64_SUB)
BUILDER_3(a64_sub_imm, AARCH64_SUB_IMM)
BUILDER_3(a64_mul, AARCH64_MUL)
BUILDER_3(a64_sdiv, AARCH64_SDIV)
BUILDER_4(a64_stp, AARCH64_STP)
BUILDER_4(a64_ldp, AARCH64_LDP)
BUILDER_4(a64_msub, AARCH64_MSUB)

// ================================================================
// Machine item -> AArch64 operand conversion
// ================================================================

static AArch64_Operand
machine_item_to_operand(const _UNIT_MachineItem *machine_item)
{
    assert(machine_item != NULL);
    if (machine_item->type == _UNIT_TYPE_REGISTER) {
        return a64_reg(register_map[machine_item->value]);
    } else if (machine_item->type == _UNIT_TYPE_CONSTANT) {
        return a64_imm(machine_item->value);
    } else if (machine_item->type == _UNIT_TYPE_CALL_ARGS) {
        printf("cannot use call args as operand\n");
        abort();
    } else {
        return a64_stack(machine_item->value * 8);
    }
}

static _UNIT_MachineItem *
generic_item_passthrough(_UNIT_MachineItem *item)
{
    assert(item != NULL);
    return item;
}

#define ENSURE_VALID_ITEM(item)                                                         \
    _Generic((item),                                                                    \
             _UNIT_MachineItem*: generic_item_passthrough,                              \
             _UNIT_MachineDestination: _UNIT_MachineDestination_GetPointerNullable      \
            )(item)

// ================================================================
// Emit macro
// ================================================================

#define EMIT(op)                                                             \
    if (UNIT_FAILED(AArch64_encode_instruction(compile_context, op))) {      \
        return _UNIT_FAIL;                                                   \
    }

// ================================================================
// Load/store helpers for stack slots
// ================================================================

// On AArch64, stack slots are accessed relative to SP.
// The prologue sets up the frame so that local slots start at SP + 16
// (FP/LR are at the bottom of the frame).
// The stack frame allocator returns byte offsets that are multiples of 8.
// We use SP-relative LDR/STR with unsigned immediate offset.
// The offset in the slot already accounts for bytes (value * 8).
// We add 16 to skip over the saved FP/LR.

static UNIT_Status
load_from_stack(_UNIT_CompileContext *compile_context,
                AArch64_Register dst, UNIT_Size byte_offset)
{
    UNIT_Size actual_offset = byte_offset + 16; // skip FP/LR
    EMIT(a64_ldr(compile_context->context, a64_reg(dst), a64_reg(REG_SP),
                 a64_imm(actual_offset)));
    return _UNIT_OK;
}

static UNIT_Status
store_to_stack(_UNIT_CompileContext *compile_context,
               AArch64_Register src, UNIT_Size byte_offset)
{
    UNIT_Size actual_offset = byte_offset + 16; // skip FP/LR
    EMIT(a64_str(compile_context->context, a64_reg(src), a64_reg(REG_SP),
                 a64_imm(actual_offset)));
    return _UNIT_OK;
}

// ================================================================
// Ensure value is in a register
// ================================================================

static UNIT_Status
ensure_in_register(_UNIT_CompileContext *compile_context,
                   _UNIT_MachineItem *item, AArch64_Register target,
                   AArch64_Register *out_reg)
{
    assert(compile_context != NULL);
    assert(item != NULL);
    assert(out_reg != NULL);
    UNIT_Context *ctx = compile_context->context;

    AArch64_Operand op = machine_item_to_operand(item);

    if (op.kind == A64_OPERAND_REGISTER) {
        *out_reg = op.reg;
        return _UNIT_OK;
    }

    if (op.kind == A64_OPERAND_IMMEDIATE) {
        EMIT(a64_mov_imm(ctx, a64_reg(target), a64_imm(op.immediate)));
        *out_reg = target;
        return _UNIT_OK;
    }

    // Stack slot
    assert(op.kind == A64_OPERAND_STACK);
    if (UNIT_FAILED(load_from_stack(compile_context, target, op.immediate))) {
        return _UNIT_FAIL;
    }
    *out_reg = target;
    return _UNIT_OK;
}

static UNIT_Status
write_back_if_needed(_UNIT_CompileContext *compile_context,
                     _UNIT_MachineItem *item, AArch64_Register actual_reg)
{
    AArch64_Operand original = machine_item_to_operand(item);
    if (original.kind == A64_OPERAND_STACK) {
        return store_to_stack(compile_context, actual_reg, original.immediate);
    }
    return _UNIT_OK;
}

// ================================================================
// Register save/restore for call sequences
// ================================================================

static UNIT_Status
preserve_register(_UNIT_CompileContext *compile_context,
                  _UNIT_MachineOperation *operation, AArch64_Register to_preserve,
                  UNIT_Size *slot_ptr)
{
    assert(compile_context != NULL);
    assert(operation != NULL);
    assert(slot_ptr != NULL);

#define IGNORE_IF_TARGET(val)                                        \
    if ((val) != NULL                                                \
        && (val)->type == _UNIT_TYPE_REGISTER                        \
        && register_map[(val)->value] == to_preserve) {              \
        *slot_ptr = (UNIT_Size)-1;                                   \
        return _UNIT_OK;                                             \
    }

    IGNORE_IF_TARGET(_UNIT_MachineDestination_GetPointerNullable(operation->destination));
    IGNORE_IF_TARGET(operation->argument_1);
    IGNORE_IF_TARGET(operation->argument_2);

    UNIT_Size slot = _UNIT_StackFrame_AllocateSlot(&compile_context->stack_frame);
    if (UNIT_FAILED(store_to_stack(compile_context, to_preserve, slot))) {
        return _UNIT_FAIL;
    }
    *slot_ptr = slot;

    return _UNIT_OK;

#undef IGNORE_IF_TARGET
}

static UNIT_Status
restore_register(_UNIT_CompileContext *compile_context, AArch64_Register preserved,
                 UNIT_Size slot)
{
    assert(compile_context != NULL);
    if (slot == (UNIT_Size)-1) {
        return _UNIT_OK;
    }

    if (UNIT_FAILED(load_from_stack(compile_context, preserved, slot))) {
        return _UNIT_FAIL;
    }
    _UNIT_StackFrame_FreeSlot(&compile_context->stack_frame, slot);
    return _UNIT_OK;
}

// ================================================================
// Main translation function
// ================================================================

static UNIT_Status
translate_operation(_UNIT_CompileContext *compile_context,
                    _UNIT_MachineOperation *operation,
                    UNIT_ABI abi,
                    _UNIT_SizeVector *epilogue_patches)
{
    assert(compile_context != NULL);
    assert(operation != NULL);
    assert(epilogue_patches != NULL);
    UNIT_Context *ctx = compile_context->context;

    (void)abi; // AArch64 calling convention is the same for SystemV and Apple
               // for basic integer args

#define OP(value) machine_item_to_operand(ENSURE_VALID_ITEM(operation->value))

    switch (operation->instruction) {

        case _UNIT_I_MOVE: {
            AArch64_Operand dst = OP(destination);
            AArch64_Operand src = OP(argument_1);
            assert(dst.kind != A64_OPERAND_IMMEDIATE);

            AArch64_Register src_reg;
            if (src.kind == A64_OPERAND_REGISTER) {
                src_reg = src.reg;
            } else if (src.kind == A64_OPERAND_IMMEDIATE) {
                EMIT(a64_mov_imm(ctx, a64_reg(SCRATCH_REG), src));
                src_reg = SCRATCH_REG;
            } else {
                // stack
                if (UNIT_FAILED(load_from_stack(compile_context, SCRATCH_REG, src.immediate))) {
                    return _UNIT_FAIL;
                }
                src_reg = SCRATCH_REG;
            }

            if (dst.kind == A64_OPERAND_REGISTER) {
                if (src_reg != dst.reg) {
                    EMIT(a64_mov_reg(ctx, a64_reg(dst.reg), a64_reg(src_reg)));
                }
            } else {
                // stack
                if (UNIT_FAILED(store_to_stack(compile_context, src_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_CALL_SYMBOL: {
            assert(operation->argument_2->type == _UNIT_TYPE_CALL_ARGS);
            _UNIT_Vector *arguments = operation->argument_2->call_args;
            UNIT_Size num_arguments = _UNIT_Vector_SIZE(arguments);
            assert(num_arguments <= 8);

            // Preserve X0 if it's live
            UNIT_Size slot_x0;
            if (UNIT_FAILED(preserve_register(compile_context, operation, REG_X0, &slot_x0))) {
                return _UNIT_FAIL;
            }

            // Save allocatable registers into stack slots
            UNIT_Size save_slots[8];
            for (UNIT_Size index = 0; index < 8; ++index) {
                AArch64_Register saved_register = register_map[index];
                if (OP(destination).kind == A64_OPERAND_REGISTER
                    && saved_register == OP(destination).reg) {
                    save_slots[index] = (UNIT_Size)-1;
                    continue;
                }

                save_slots[index] = _UNIT_StackFrame_AllocateSlot(&compile_context->stack_frame);
                assert(save_slots[index] % 8 == 0);
                if (UNIT_FAILED(store_to_stack(compile_context, saved_register, save_slots[index]))) {
                    return _UNIT_FAIL;
                }
            }

            // Load arguments into X0-X7
            for (UNIT_Size argument = 0; argument < num_arguments; ++argument) {
                AArch64_Register arg_reg = argument_registers[argument];
                _UNIT_MachineItem *arg_item = _UNIT_Vector_GET(arguments, argument);
                AArch64_Operand value = machine_item_to_operand(arg_item);

                // Load from save slots to avoid circular dependency issues
                if (value.kind == A64_OPERAND_REGISTER) {
                    for (UNIT_Size index = 0; index < 8; ++index) {
                        if (save_slots[index] != (UNIT_Size)-1
                            && register_map[index] == value.reg) {
                            value = a64_stack(save_slots[index]);
                            break;
                        }
                    }
                }

                if (value.kind == A64_OPERAND_REGISTER) {
                    if (value.reg != arg_reg) {
                        EMIT(a64_mov_reg(ctx, a64_reg(arg_reg), a64_reg(value.reg)));
                    }
                } else if (value.kind == A64_OPERAND_IMMEDIATE) {
                    EMIT(a64_mov_imm(ctx, a64_reg(arg_reg), value));
                } else {
                    // stack
                    if (UNIT_FAILED(load_from_stack(compile_context, arg_reg, value.immediate))) {
                        return _UNIT_FAIL;
                    }
                }
            }

            // Emit call via relocation
            EMIT(a64_call_symbol(ctx, OP(argument_1)));

            // Move result from X0 to destination
            AArch64_Operand dst = OP(destination);
            if (dst.kind == A64_OPERAND_REGISTER) {
                if (dst.reg != REG_X0) {
                    EMIT(a64_mov_reg(ctx, a64_reg(dst.reg), a64_reg(REG_X0)));
                }
            } else {
                if (UNIT_FAILED(store_to_stack(compile_context, REG_X0, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }

            // Restore registers
            for (UNIT_Size index = 0; index < 8; ++index) {
                if (save_slots[index] == (UNIT_Size)-1) {
                    continue;
                }
                AArch64_Register saved_register = register_map[index];
                if (UNIT_FAILED(load_from_stack(compile_context, saved_register, save_slots[index]))) {
                    return _UNIT_FAIL;
                }
                _UNIT_StackFrame_FreeSlot(&compile_context->stack_frame, save_slots[index]);
            }

            if (UNIT_FAILED(restore_register(compile_context, REG_X0, slot_x0))) {
                return _UNIT_FAIL;
            }

            break;
        }

        case _UNIT_I_LOAD_STRING: {
            AArch64_Operand dst = OP(destination);
            AArch64_Register dst_reg;
            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            EMIT(a64_load_string(ctx, a64_reg(dst_reg), OP(argument_1)));

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_EXIT: {
            // On AArch64 Linux, exit syscall is SVC #0 with X8=93, X0=status.
            // But for JIT we just return the value. We'll treat exit as a return for now.
            AArch64_Operand status = OP(destination);
            AArch64_Register status_reg;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->destination), SCRATCH_REG, &status_reg))) {
                return _UNIT_FAIL;
            }
            if (status_reg != REG_X0) {
                EMIT(a64_mov_reg(ctx, a64_reg(REG_X0), a64_reg(status_reg)));
            }
            // Epilogue + ret
            UNIT_Size patch_offset = _UNIT_CodeBuffer_Reserve(&compile_context->buffer, 8);
            if (UNIT_FAILED(_UNIT_SizeVector_Append(epilogue_patches, patch_offset))) {
                return _UNIT_FAIL;
            }
            EMIT(a64_ret(ctx));
            break;
        }

        case _UNIT_I_RETURN_VALUE: {
            AArch64_Register src_reg;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &src_reg))) {
                return _UNIT_FAIL;
            }
            if (src_reg != REG_X0) {
                EMIT(a64_mov_reg(ctx, a64_reg(REG_X0), a64_reg(src_reg)));
            }
            // Reserve 8 bytes for the epilogue (2 instructions)
            UNIT_Size patch_offset = _UNIT_CodeBuffer_Reserve(&compile_context->buffer, 8);
            if (UNIT_FAILED(_UNIT_SizeVector_Append(epilogue_patches, patch_offset))) {
                return _UNIT_FAIL;
            }
            EMIT(a64_ret(ctx));
            break;
        }

        case _UNIT_I_LOAD_ARGUMENT: {
            UNIT_Size arg_index = operation->argument_1->value;
            assert(arg_index < 8);
            AArch64_Register arg_reg = argument_registers[arg_index];
            AArch64_Operand dst = OP(destination);

            if (dst.kind == A64_OPERAND_REGISTER) {
                EMIT(a64_mov_reg(ctx, a64_reg(dst.reg), a64_reg(arg_reg)));
            } else {
                if (UNIT_FAILED(store_to_stack(compile_context, arg_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_COMPARE_EQUAL: {
            AArch64_Register lhs, rhs;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_2), SCRATCH_REG, &lhs))) {
                return _UNIT_FAIL;
            }
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), REG_X17, &rhs))) {
                return _UNIT_FAIL;
            }
            EMIT(a64_cmp(ctx, a64_reg(lhs), a64_reg(rhs)));
            break;
        }

        case _UNIT_I_JUMP: {
            EMIT(a64_b(ctx, OP(argument_1)));
            break;
        }

        case _UNIT_I_JUMP_LABEL: {
            EMIT(a64_label(ctx, OP(destination)));
            break;
        }

#define JUMP_CONDITION(inst, cond_code)                                               \
        case inst: {                                                                  \
            AArch64_Register lhs, rhs;                                                \
            if (UNIT_FAILED(ensure_in_register(compile_context,                       \
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &lhs))) {   \
                return _UNIT_FAIL;                                                    \
            }                                                                         \
            if (UNIT_FAILED(ensure_in_register(compile_context,                       \
                    ENSURE_VALID_ITEM(operation->argument_2), REG_X17, &rhs))) {       \
                return _UNIT_FAIL;                                                    \
            }                                                                         \
            EMIT(a64_cmp(ctx, a64_reg(lhs), a64_reg(rhs)));                           \
            EMIT(a64_b_cond(ctx, a64_cond(cond_code), OP(destination)));              \
            break;                                                                    \
        }

        JUMP_CONDITION(_UNIT_I_JUMP_IF_EQUAL, A64_COND_EQ)
        JUMP_CONDITION(_UNIT_I_JUMP_IF_NOT_EQUAL, A64_COND_NE)
        JUMP_CONDITION(_UNIT_I_JUMP_IF_LESS, A64_COND_LT)
        JUMP_CONDITION(_UNIT_I_JUMP_IF_LESS_EQUAL, A64_COND_LE)
        JUMP_CONDITION(_UNIT_I_JUMP_IF_GREATER, A64_COND_GT)
        JUMP_CONDITION(_UNIT_I_JUMP_IF_GREATER_EQUAL, A64_COND_GE)

#undef JUMP_CONDITION

        case _UNIT_I_ADD: {
            AArch64_Operand dst = OP(destination);
            AArch64_Register dst_reg;
            AArch64_Register lhs, rhs;

            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &lhs))) {
                return _UNIT_FAIL;
            }
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_2), REG_X17, &rhs))) {
                return _UNIT_FAIL;
            }

            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            EMIT(a64_add(ctx, a64_reg(dst_reg), a64_reg(lhs), a64_reg(rhs)));

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_SUB: {
            AArch64_Operand dst = OP(destination);
            AArch64_Register dst_reg;
            AArch64_Register lhs, rhs;

            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &lhs))) {
                return _UNIT_FAIL;
            }
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_2), REG_X17, &rhs))) {
                return _UNIT_FAIL;
            }

            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            EMIT(a64_sub(ctx, a64_reg(dst_reg), a64_reg(lhs), a64_reg(rhs)));

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_MUL: {
            AArch64_Operand dst = OP(destination);
            AArch64_Register dst_reg;
            AArch64_Register lhs, rhs;

            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &lhs))) {
                return _UNIT_FAIL;
            }
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_2), REG_X17, &rhs))) {
                return _UNIT_FAIL;
            }

            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            EMIT(a64_mul(ctx, a64_reg(dst_reg), a64_reg(lhs), a64_reg(rhs)));

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_DIV: {
            AArch64_Operand dst = OP(destination);
            AArch64_Register dst_reg;
            AArch64_Register lhs, rhs;

            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &lhs))) {
                return _UNIT_FAIL;
            }
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_2), REG_X17, &rhs))) {
                return _UNIT_FAIL;
            }

            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            EMIT(a64_sdiv(ctx, a64_reg(dst_reg), a64_reg(lhs), a64_reg(rhs)));

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_MOD: {
            // remainder = lhs - (lhs / rhs) * rhs
            // AArch64 has MSUB: Xd = Xa - Xn * Xm
            // So: quotient = sdiv(lhs, rhs);  remainder = msub(lhs, quotient, rhs)
            AArch64_Operand dst = OP(destination);
            AArch64_Register dst_reg;
            AArch64_Register lhs, rhs;

            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &lhs))) {
                return _UNIT_FAIL;
            }
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_2), REG_X17, &rhs))) {
                return _UNIT_FAIL;
            }

            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            // quotient in X17 (reuse scratch)
            // But X17 might already hold rhs, so use a different temp
            AArch64_Register quot_reg = REG_X16;
            if (lhs == REG_X16) {
                // lhs is in X16, use X17 for quotient
                quot_reg = REG_X17;
            }

            // sdiv quot_reg, lhs, rhs
            EMIT(a64_sdiv(ctx, a64_reg(quot_reg), a64_reg(lhs), a64_reg(rhs)));
            // msub dst_reg, quot_reg, rhs, lhs  =>  dst = lhs - quot*rhs
            EMIT(a64_msub(ctx, a64_reg(dst_reg), a64_reg(quot_reg), a64_reg(rhs),
                          a64_reg(lhs)));

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_CONVERT: {
            AArch64_Register src_reg;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &src_reg))) {
                return _UNIT_FAIL;
            }

            UNIT_IntegerType target = operation->argument_2->value;
            AArch64_Register result_reg = SCRATCH_REG;

            switch (target) {
                case UNIT_TYPE_UINT8:
                    EMIT(a64_uxtb(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    break;
                case UNIT_TYPE_INT8:
                    EMIT(a64_sxtb(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    break;
                case UNIT_TYPE_UINT16:
                    EMIT(a64_uxth(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    break;
                case UNIT_TYPE_INT16:
                    EMIT(a64_sxth(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    break;
                case UNIT_TYPE_UINT32:
                    EMIT(a64_mov_w(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    break;
                case UNIT_TYPE_INT32:
                    EMIT(a64_sxtw(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    break;
                case UNIT_TYPE_UINT64:
                case UNIT_TYPE_INT64:
                    if (src_reg != result_reg) {
                        EMIT(a64_mov_reg(ctx, a64_reg(result_reg), a64_reg(src_reg)));
                    }
                    break;
            }

            // Write result to destination
            AArch64_Operand dst = OP(destination);
            if (dst.kind == A64_OPERAND_REGISTER) {
                if (result_reg != dst.reg) {
                    EMIT(a64_mov_reg(ctx, a64_reg(dst.reg), a64_reg(result_reg)));
                }
            } else {
                if (UNIT_FAILED(store_to_stack(compile_context, result_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_READ_BYTES: {
            AArch64_Register addr_reg;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), SCRATCH_REG, &addr_reg))) {
                return _UNIT_FAIL;
            }

            UNIT_Size size = operation->argument_2->value;
            AArch64_Register result_reg = SCRATCH_REG;

            switch (size) {
                case 1:
                    EMIT(a64_ldrb(ctx, a64_reg(result_reg), a64_reg(addr_reg)));
                    break;
                case 2:
                    EMIT(a64_ldrh(ctx, a64_reg(result_reg), a64_reg(addr_reg)));
                    break;
                case 4:
                    EMIT(a64_ldr_w(ctx, a64_reg(result_reg), a64_reg(addr_reg)));
                    break;
                case 8:
                    // ldr X, [X] with zero offset
                    EMIT(a64_ldr(ctx, a64_reg(result_reg), a64_reg(addr_reg), a64_imm(0)));
                    break;
                default:
                    _UNIT_Unreachable();
            }

            AArch64_Operand dst = OP(destination);
            if (dst.kind == A64_OPERAND_REGISTER) {
                if (result_reg != dst.reg) {
                    EMIT(a64_mov_reg(ctx, a64_reg(dst.reg), a64_reg(result_reg)));
                }
            } else {
                if (UNIT_FAILED(store_to_stack(compile_context, result_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }

        case _UNIT_I_WRITE_BYTES: {
            AArch64_Register addr_reg;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->destination), SCRATCH_REG, &addr_reg))) {
                return _UNIT_FAIL;
            }

            AArch64_Register val_reg;
            if (UNIT_FAILED(ensure_in_register(compile_context,
                    ENSURE_VALID_ITEM(operation->argument_1), REG_X17, &val_reg))) {
                return _UNIT_FAIL;
            }

            UNIT_Size size = operation->argument_2->value;

            switch (size) {
                case 1:
                    EMIT(a64_strb(ctx, a64_reg(val_reg), a64_reg(addr_reg)));
                    break;
                case 2:
                    EMIT(a64_strh(ctx, a64_reg(val_reg), a64_reg(addr_reg)));
                    break;
                case 4:
                    EMIT(a64_str_w(ctx, a64_reg(val_reg), a64_reg(addr_reg)));
                    break;
                case 8:
                    EMIT(a64_str(ctx, a64_reg(val_reg), a64_reg(addr_reg), a64_imm(0)));
                    break;
                default:
                    _UNIT_Unreachable();
            }
            break;
        }

        case _UNIT_I_ADDRESS_OF: {
            // LEA equivalent: compute address of stack slot
            AArch64_Operand src = OP(argument_1);
            assert(src.kind == A64_OPERAND_STACK);
            AArch64_Operand dst = OP(destination);

            AArch64_Register dst_reg;
            if (dst.kind == A64_OPERAND_REGISTER) {
                dst_reg = dst.reg;
            } else {
                dst_reg = SCRATCH_REG;
            }

            UNIT_Size actual_offset = src.immediate + 16;
            if (actual_offset < 4096) {
                EMIT(a64_add_imm(ctx, a64_reg(dst_reg), a64_reg(REG_SP),
                                 a64_imm(actual_offset)));
            } else {
                EMIT(a64_mov_imm(ctx, a64_reg(dst_reg), a64_imm(actual_offset)));
                EMIT(a64_add(ctx, a64_reg(dst_reg), a64_reg(REG_SP), a64_reg(dst_reg)));
            }

            if (dst.kind != A64_OPERAND_REGISTER) {
                if (UNIT_FAILED(store_to_stack(compile_context, dst_reg, dst.immediate))) {
                    return _UNIT_FAIL;
                }
            }
            break;
        }
    }

    return _UNIT_OK;
#undef OP
}

static void
patch_epilogues(_UNIT_CompileContext *compile_context, _UNIT_SizeVector *epilogue_patches,
                UNIT_Size frame_size)
{
    assert(compile_context != NULL);
    assert(epilogue_patches != NULL);
    UNIT_Size size = _UNIT_SizeVector_SIZE(epilogue_patches);
    for (UNIT_Size index = 0; index < size; ++index) {
        UNIT_Size epilogue_offset = _UNIT_SizeVector_GET(epilogue_patches, index);
        AArch64_PatchEpilogue(compile_context, epilogue_offset, frame_size);
    }
}

UNIT_Status
_UNIT_AArch64_Compile(_UNIT_Translation *translation,
                      _UNIT_CompileContext *compile_context,
                      UNIT_ABI abi)
{
    // Reserve space for the prologue: stp + mov = 8 bytes
    UNIT_Size prologue_offset = _UNIT_CodeBuffer_Reserve(&compile_context->buffer, 8);

    _UNIT_SizeVector epilogue_patches;
    if (UNIT_FAILED(_UNIT_SizeVector_Init(&epilogue_patches, compile_context->context, 4))) {
        return _UNIT_FAIL;
    }

    assert(translation != NULL);
    UNIT_Size blocks_size = _UNIT_Vector_SIZE(&translation->blocks);
    for (UNIT_Size block_index = 0; block_index < blocks_size; ++block_index) {
        _UNIT_BasicBlock *block = _UNIT_Vector_GET(&translation->blocks, block_index);
        assert(block != NULL);
        UNIT_Size instructions_size = _UNIT_Vector_SIZE(&block->instructions);
        for (UNIT_Size index = 0; index < instructions_size; ++index) {
            _UNIT_MachineOperation *operation = _UNIT_Vector_GET(&block->instructions,
                                                                index);
            assert(operation != NULL);
            if (UNIT_FAILED(translate_operation(compile_context, operation, abi, &epilogue_patches))) {
                _UNIT_SizeVector_Clear(&epilogue_patches);
                return _UNIT_FAIL;
            }
        }
    }

    UNIT_Size frame_size = _UNIT_StackFrame_ComputeSize(&compile_context->stack_frame);
    AArch64_PatchPrologue(compile_context, prologue_offset, frame_size);
    patch_epilogues(compile_context, &epilogue_patches, frame_size);
    AArch64_PatchJumps(compile_context);

    _UNIT_SizeVector_Clear(&epilogue_patches);
    return _UNIT_OK;
}