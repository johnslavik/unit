#include <unit/base.h>

#include <unit/internal/architectures.h>
#include <unit/internal/code_buffer.h>
#include <unit/internal/compile_context.h>
#include <stdio.h>
#include "aarch64_local.h"

// AArch64 instructions are always 4 bytes (32-bit fixed width).
// All instruction encoding helpers emit a single 32-bit word.

static UNIT_Status
emit32(_UNIT_CompileContext *ctx, uint32_t value)
{
    // AArch64 instructions are little-endian 32-bit words.
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit8(&ctx->buffer, (value >>  0) & 0xFF))) return _UNIT_FAIL;
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit8(&ctx->buffer, (value >>  8) & 0xFF))) return _UNIT_FAIL;
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit8(&ctx->buffer, (value >> 16) & 0xFF))) return _UNIT_FAIL;
    if (UNIT_FAILED(_UNIT_CodeBuffer_Emit8(&ctx->buffer, (value >> 24) & 0xFF))) return _UNIT_FAIL;
    return _UNIT_OK;
}

static void
patch32(_UNIT_CompileContext *ctx, UNIT_Size offset, uint32_t value)
{
    ctx->buffer.data[offset + 0] = (value >>  0) & 0xFF;
    ctx->buffer.data[offset + 1] = (value >>  8) & 0xFF;
    ctx->buffer.data[offset + 2] = (value >> 16) & 0xFF;
    ctx->buffer.data[offset + 3] = (value >> 24) & 0xFF;
}

static uint32_t
read32(_UNIT_CompileContext *ctx, UNIT_Size offset)
{
    return (uint32_t)ctx->buffer.data[offset + 0]
         | ((uint32_t)ctx->buffer.data[offset + 1] << 8)
         | ((uint32_t)ctx->buffer.data[offset + 2] << 16)
         | ((uint32_t)ctx->buffer.data[offset + 3] << 24);
}

// Helper to record a pending jump for later patching
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
        return _UNIT_FAIL;
    }

    if (UNIT_FAILED(_UNIT_Vector_Append(&compile_context->jump_table.pending_jumps,
                                        jump))) {
        return _UNIT_FAIL;
    }

    return _UNIT_OK;
}

// ================================================================
// AArch64 encoding helpers
// ================================================================

// MOV (register): ORR Xd, XZR, Xm
static uint32_t
encode_mov_reg(AArch64_Register rd, AArch64_Register rm)
{
    // ORR (shifted register), 64-bit: sf=1, opc=01, shift=00, N=0
    // 1 01 01010 00 0 Rm 000000 11111 Rd
    return 0xAA0003E0 | ((uint32_t)rm << 16) | (uint32_t)rd;
}

// MOVZ Xd, #imm16, LSL #shift
static uint32_t
encode_movz(AArch64_Register rd, uint16_t imm16, uint8_t shift)
{
    // sf=1, opc=10, hw=shift/16
    uint32_t hw = shift / 16;
    return 0xD2800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (uint32_t)rd;
}

// MOVK Xd, #imm16, LSL #shift
static uint32_t
encode_movk(AArch64_Register rd, uint16_t imm16, uint8_t shift)
{
    uint32_t hw = shift / 16;
    return 0xF2800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (uint32_t)rd;
}

// MOVN Xd, #imm16, LSL #shift (move wide with NOT)
static uint32_t
encode_movn(AArch64_Register rd, uint16_t imm16, uint8_t shift)
{
    uint32_t hw = shift / 16;
    return 0x92800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (uint32_t)rd;
}

// ADD Xd, Xn, Xm
static uint32_t
encode_add_reg(AArch64_Register rd, AArch64_Register rn, AArch64_Register rm)
{
    // sf=1, op=0, S=0
    return 0x8B000000 | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// ADD Xd, Xn, #imm12
static uint32_t
encode_add_imm(AArch64_Register rd, AArch64_Register rn, uint32_t imm12)
{
    assert(imm12 < 4096);
    return 0x91000000 | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// SUB Xd, Xn, Xm
static uint32_t
encode_sub_reg(AArch64_Register rd, AArch64_Register rn, AArch64_Register rm)
{
    return 0xCB000000 | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// SUB Xd, Xn, #imm12
static uint32_t
encode_sub_imm(AArch64_Register rd, AArch64_Register rn, uint32_t imm12)
{
    assert(imm12 < 4096);
    return 0xD1000000 | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// MUL Xd, Xn, Xm   (alias of MADD Xd, Xn, Xm, XZR)
static uint32_t
encode_mul(AArch64_Register rd, AArch64_Register rn, AArch64_Register rm)
{
    // MADD: sf=1, op54=00, op31=000, Rm, o0=0, Ra=XZR(31)
    return 0x9B007C00 | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// SDIV Xd, Xn, Xm
static uint32_t
encode_sdiv(AArch64_Register rd, AArch64_Register rn, AArch64_Register rm)
{
    // sf=1, S=0, opcode2=000110, o1=0, Rm, o0=1 (signed)
    return 0x9AC00C00 | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// MSUB Xd, Xn, Xm, Xa   (Xa - Xn*Xm)
static uint32_t
encode_msub(AArch64_Register rd, AArch64_Register rn, AArch64_Register rm,
            AArch64_Register ra)
{
    return 0x9B008000 | ((uint32_t)rm << 16) | ((uint32_t)ra << 10)
         | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// CMP Xn, Xm  (alias of SUBS XZR, Xn, Xm)
static uint32_t
encode_cmp_reg(AArch64_Register rn, AArch64_Register rm)
{
    return 0xEB00001F | ((uint32_t)rm << 16) | ((uint32_t)rn << 5);
}

// B offset  (unconditional branch, imm26 * 4)
static uint32_t
encode_b(int32_t imm26)
{
    return 0x14000000 | ((uint32_t)imm26 & 0x03FFFFFF);
}

// B.cond offset  (conditional branch, imm19 * 4)
static uint32_t
encode_b_cond(AArch64_Condition cond, int32_t imm19)
{
    return 0x54000000 | (((uint32_t)imm19 & 0x7FFFF) << 5) | (uint32_t)cond;
}

// BL offset  (branch with link)
static uint32_t
encode_bl(int32_t imm26)
{
    return 0x94000000 | ((uint32_t)imm26 & 0x03FFFFFF);
}

// BLR Xn  (branch with link to register)
static uint32_t
encode_blr(AArch64_Register rn)
{
    return 0xD63F0000 | ((uint32_t)rn << 5);
}

// RET Xn
static uint32_t
encode_ret(AArch64_Register rn)
{
    return 0xD65F0000 | ((uint32_t)rn << 5);
}

// LDR Xt, [Xn, #imm12*8]  (unsigned offset, 64-bit)
static uint32_t
encode_ldr_imm(AArch64_Register rt, AArch64_Register rn, uint32_t byte_offset)
{
    assert((byte_offset & 7) == 0);
    uint32_t imm12 = byte_offset / 8;
    assert(imm12 < 4096);
    // size=11, V=0, opc=01
    return 0xF9400000 | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// STR Xt, [Xn, #imm12*8]  (unsigned offset, 64-bit)
static uint32_t
encode_str_imm(AArch64_Register rt, AArch64_Register rn, uint32_t byte_offset)
{
    assert((byte_offset & 7) == 0);
    uint32_t imm12 = byte_offset / 8;
    assert(imm12 < 4096);
    return 0xF9000000 | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// LDRB Wt, [Xn]  (unsigned offset, zero-extending byte)
static uint32_t
encode_ldrb(AArch64_Register rt, AArch64_Register rn)
{
    return 0x39400000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// LDRH Wt, [Xn]
static uint32_t
encode_ldrh(AArch64_Register rt, AArch64_Register rn)
{
    return 0x79400000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// LDR Wt, [Xn]  (32-bit zero-extending load)
static uint32_t
encode_ldr_w(AArch64_Register rt, AArch64_Register rn)
{
    return 0xB9400000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// LDRSW Xt, [Xn]  (32-bit sign-extending load)
static uint32_t
encode_ldrsw(AArch64_Register rt, AArch64_Register rn)
{
    return 0xB9800000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// STRB Wt, [Xn]
static uint32_t
encode_strb(AArch64_Register rt, AArch64_Register rn)
{
    return 0x39000000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// STRH Wt, [Xn]
static uint32_t
encode_strh(AArch64_Register rt, AArch64_Register rn)
{
    return 0x79000000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// STR Wt, [Xn]  (32-bit store)
static uint32_t
encode_str_w(AArch64_Register rt, AArch64_Register rn)
{
    return 0xB9000000 | ((uint32_t)rn << 5) | (uint32_t)rt;
}

// STP Xt1, Xt2, [Xn, #imm7*8]!  (pre-index)
static uint32_t
encode_stp_pre(AArch64_Register rt1, AArch64_Register rt2,
               AArch64_Register rn, int32_t byte_offset)
{
    assert((byte_offset & 7) == 0);
    int32_t imm7 = byte_offset / 8;
    assert(imm7 >= -64 && imm7 < 64);
    // opc=10, V=0, type=pre-index(011)
    return 0xA9800000 | (((uint32_t)imm7 & 0x7F) << 15)
         | ((uint32_t)rt2 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt1;
}

// LDP Xt1, Xt2, [Xn], #imm7*8  (post-index)
static uint32_t
encode_ldp_post(AArch64_Register rt1, AArch64_Register rt2,
                AArch64_Register rn, int32_t byte_offset)
{
    assert((byte_offset & 7) == 0);
    int32_t imm7 = byte_offset / 8;
    assert(imm7 >= -64 && imm7 < 64);
    return 0xA8C00000 | (((uint32_t)imm7 & 0x7F) << 15)
         | ((uint32_t)rt2 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt1;
}

// SXTB Xd, Wn  (sign extend byte to 64-bit)
static uint32_t
encode_sxtb(AArch64_Register rd, AArch64_Register rn)
{
    // SBFM Xd, Xn, #0, #7
    return 0x93401C00 | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// SXTH Xd, Wn
static uint32_t
encode_sxth(AArch64_Register rd, AArch64_Register rn)
{
    // SBFM Xd, Xn, #0, #15
    return 0x93403C00 | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// SXTW Xd, Wn
static uint32_t
encode_sxtw(AArch64_Register rd, AArch64_Register rn)
{
    // SBFM Xd, Xn, #0, #31
    return 0x93407C00 | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// UXTB Xd, Wn  (zero extend byte: AND Xd, Xn, #0xFF)
static uint32_t
encode_uxtb(AArch64_Register rd, AArch64_Register rn)
{
    // UBFM Wd, Wn, #0, #7  (32-bit to zero-extend upper 32 bits too)
    return 0x53001C00 | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// UXTH Xd, Wn
static uint32_t
encode_uxth(AArch64_Register rd, AArch64_Register rn)
{
    // UBFM Wd, Wn, #0, #15
    return 0x53003C00 | ((uint32_t)rn << 5) | (uint32_t)rd;
}

// MOV Wd, Wn  (32-bit move, zero extends upper 32)
static uint32_t
encode_mov_w(AArch64_Register rd, AArch64_Register rn)
{
    // ORR Wd, WZR, Wn
    return 0x2A0003E0 | ((uint32_t)rn << 16) | (uint32_t)rd;
}

// ADR Xd, #imm (PC-relative, +/- 1MB)
static uint32_t
encode_adr(AArch64_Register rd, int32_t imm21)
{
    uint32_t immlo = (uint32_t)imm21 & 0x3;
    uint32_t immhi = ((uint32_t)imm21 >> 2) & 0x7FFFF;
    return 0x10000000 | (immlo << 29) | (immhi << 5) | (uint32_t)rd;
}

// NOP
static uint32_t
encode_nop(void)
{
    return 0xD503201F;
}

// Emit a full 64-bit immediate into a register using MOVZ/MOVN + MOVK
static UNIT_Status
emit_mov_imm64(_UNIT_CompileContext *ctx, AArch64_Register rd, int64_t value)
{
    uint64_t uval = (uint64_t)value;

    // Check if a MOVN sequence is shorter (value has many leading 1 bits)
    uint64_t inverted = ~uval;
    int use_movn = 0;
    uint64_t working_val = uval;

    // Count trailing all-ones 16-bit chunks for MOVN
    if ((uval >> 48) == 0xFFFF && (uval >> 32 & 0xFFFF) == 0xFFFF
        && (uval >> 16 & 0xFFFF) == 0xFFFF) {
        // Only bottom 16 bits differ: MOVN with inverted bottom
        use_movn = 1;
        working_val = inverted;
    } else if ((uval >> 48) == 0xFFFF && (uval >> 32 & 0xFFFF) == 0xFFFF) {
        use_movn = 1;
        working_val = inverted;
    } else if ((uval >> 48) == 0xFFFF) {
        use_movn = 1;
        working_val = inverted;
    }

    int first = 1;
    for (int shift = 0; shift < 64; shift += 16) {
        uint16_t chunk = (working_val >> shift) & 0xFFFF;
        if (first) {
            if (use_movn) {
                if (UNIT_FAILED(emit32(ctx, encode_movn(rd, chunk, shift)))) return _UNIT_FAIL;
            } else {
                if (UNIT_FAILED(emit32(ctx, encode_movz(rd, chunk, shift)))) return _UNIT_FAIL;
            }
            first = 0;
        } else if (chunk != 0 || (use_movn && chunk != 0xFFFF)) {
            // For MOVN, we need MOVK for non-0xFFFF chunks
            // For MOVZ, we need MOVK for non-zero chunks
            uint16_t movk_val = (uval >> shift) & 0xFFFF;
            if (UNIT_FAILED(emit32(ctx, encode_movk(rd, movk_val, shift)))) return _UNIT_FAIL;
        }
    }

    return _UNIT_OK;
}

UNIT_Status
AArch64_encode_instruction(_UNIT_CompileContext *compile_context,
                           AArch64_Instruction *instr)
{
    assert(compile_context != NULL);
    assert(instr != NULL);

#define EMIT(val)                                        \
    if (UNIT_FAILED(emit32(compile_context, val))) {     \
        goto error;                                      \
    }
#define INDEX() _UNIT_CodeBuffer_CurrentIndex(&compile_context->buffer)
#define EMIT_JUMP(label_index)                                           \
    if (UNIT_FAILED(add_jump(compile_context, label_index))) {           \
        goto error;                                                      \
    }

    switch (instr->opcode) {

        case AARCH64_MOV_REG: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rm = instr->operands[1].reg;
            EMIT(encode_mov_reg(rd, rm));
            break;
        }

        case AARCH64_MOV_IMM: {
            AArch64_Register rd = instr->operands[0].reg;
            int64_t value = instr->operands[1].immediate;
            if (UNIT_FAILED(emit_mov_imm64(compile_context, rd, value))) {
                goto error;
            }
            break;
        }

        case AARCH64_MOVK: {
            AArch64_Register rd = instr->operands[0].reg;
            uint16_t imm16 = (uint16_t)instr->operands[1].immediate;
            uint8_t shift = (uint8_t)instr->operands[2].immediate;
            EMIT(encode_movk(rd, imm16, shift));
            break;
        }

        case AARCH64_LDR: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            uint32_t offset = (uint32_t)instr->operands[2].immediate;
            EMIT(encode_ldr_imm(rt, rn, offset));
            break;
        }

        case AARCH64_STR: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            uint32_t offset = (uint32_t)instr->operands[2].immediate;
            EMIT(encode_str_imm(rt, rn, offset));
            break;
        }

        case AARCH64_LDRB: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_ldrb(rt, rn));
            break;
        }

        case AARCH64_LDRH: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_ldrh(rt, rn));
            break;
        }

        case AARCH64_LDR_W: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_ldr_w(rt, rn));
            break;
        }

        case AARCH64_LDRSW: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_ldrsw(rt, rn));
            break;
        }

        case AARCH64_STRB: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_strb(rt, rn));
            break;
        }

        case AARCH64_STRH: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_strh(rt, rn));
            break;
        }

        case AARCH64_STR_W: {
            AArch64_Register rt = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_str_w(rt, rn));
            break;
        }

        case AARCH64_ADD: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            AArch64_Register rm = instr->operands[2].reg;
            EMIT(encode_add_reg(rd, rn, rm));
            break;
        }

        case AARCH64_ADD_IMM: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            uint32_t imm12 = (uint32_t)instr->operands[2].immediate;
            EMIT(encode_add_imm(rd, rn, imm12));
            break;
        }

        case AARCH64_SUB: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            AArch64_Register rm = instr->operands[2].reg;
            EMIT(encode_sub_reg(rd, rn, rm));
            break;
        }

        case AARCH64_SUB_IMM: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            uint32_t imm12 = (uint32_t)instr->operands[2].immediate;
            EMIT(encode_sub_imm(rd, rn, imm12));
            break;
        }

        case AARCH64_MUL: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            AArch64_Register rm = instr->operands[2].reg;
            EMIT(encode_mul(rd, rn, rm));
            break;
        }

        case AARCH64_SDIV: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            AArch64_Register rm = instr->operands[2].reg;
            EMIT(encode_sdiv(rd, rn, rm));
            break;
        }

        case AARCH64_MSUB: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            AArch64_Register rm = instr->operands[2].reg;
            AArch64_Register ra = instr->operands[3].reg;
            EMIT(encode_msub(rd, rn, rm, ra));
            break;
        }

        case AARCH64_CMP: {
            AArch64_Register rn = instr->operands[0].reg;
            AArch64_Register rm = instr->operands[1].reg;
            EMIT(encode_cmp_reg(rn, rm));
            break;
        }

        case AARCH64_B: {
            UNIT_Size label_index = instr->operands[0].immediate;
            EMIT_JUMP(label_index);
            EMIT(encode_b(0)); // placeholder, will be patched
            break;
        }

        case AARCH64_B_COND: {
            AArch64_Condition cond = instr->operands[0].condition;
            UNIT_Size label_index = instr->operands[1].immediate;
            EMIT_JUMP(label_index);
            EMIT(encode_b_cond(cond, 0)); // placeholder, will be patched
            break;
        }

        case AARCH64_BL: {
            int32_t offset = (int32_t)instr->operands[0].immediate;
            EMIT(encode_bl(offset));
            break;
        }

        case AARCH64_BLR: {
            AArch64_Register rn = instr->operands[0].reg;
            EMIT(encode_blr(rn));
            break;
        }

        case AARCH64_RET: {
            EMIT(encode_ret(REG_LR));
            break;
        }

        case AARCH64_LABEL: {
            UNIT_Size label_index = instr->operands[0].immediate;
            if (UNIT_FAILED(_UNIT_SizeMap_Set(&compile_context->jump_table.label_offsets,
                                              label_index,
                                              INDEX()))) {
                goto error;
            }
            break;
        }

        case AARCH64_ADR: {
            AArch64_Register rd = instr->operands[0].reg;
            int32_t imm = (int32_t)instr->operands[1].immediate;
            EMIT(encode_adr(rd, imm));
            break;
        }

        case AARCH64_STP: {
            AArch64_Register rt1 = instr->operands[0].reg;
            AArch64_Register rt2 = instr->operands[1].reg;
            AArch64_Register rn = instr->operands[2].reg;
            int32_t offset = (int32_t)instr->operands[3].immediate;
            EMIT(encode_stp_pre(rt1, rt2, rn, offset));
            break;
        }

        case AARCH64_LDP: {
            AArch64_Register rt1 = instr->operands[0].reg;
            AArch64_Register rt2 = instr->operands[1].reg;
            AArch64_Register rn = instr->operands[2].reg;
            int32_t offset = (int32_t)instr->operands[3].immediate;
            EMIT(encode_ldp_post(rt1, rt2, rn, offset));
            break;
        }

        case AARCH64_SXTB: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_sxtb(rd, rn));
            break;
        }

        case AARCH64_SXTH: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_sxth(rd, rn));
            break;
        }

        case AARCH64_SXTW: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_sxtw(rd, rn));
            break;
        }

        case AARCH64_UXTB: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_uxtb(rd, rn));
            break;
        }

        case AARCH64_UXTH: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_uxth(rd, rn));
            break;
        }

        case AARCH64_MOV_W: {
            AArch64_Register rd = instr->operands[0].reg;
            AArch64_Register rn = instr->operands[1].reg;
            EMIT(encode_mov_w(rd, rn));
            break;
        }

        case AARCH64_CALL_SYMBOL: {
            // Emit a BL with a placeholder offset. We record a relocation
            // so the JIT or Mach-O writer can resolve it.
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
            EMIT(encode_bl(0)); // placeholder
            break;
        }

        case AARCH64_LOAD_STRING: {
            AArch64_Register rd = instr->operands[0].reg;
            UNIT_Size string_index = instr->operands[1].immediate;
            _UNIT_SizeMap *string_offsets = &compile_context->string_data.string_offsets;
            UNIT_Size byte_offset = _UNIT_SizeMap_GET(string_offsets, string_index);

            // We'll emit an ADR with a placeholder. Record a relocation
            // so the JIT or Mach-O writer can resolve it.
            _UNIT_Relocation *relocation = _UNIT_Relocation_NewData(
                compile_context->context, INDEX(), byte_offset);
            if (relocation == NULL) {
                goto error;
            }
            if (UNIT_FAILED(_UNIT_Vector_Append(&compile_context->symbol_table.relocations,
                                                relocation))) {
                goto error;
            }
            EMIT(encode_adr(rd, 0)); // placeholder
            break;
        }

        case AARCH64_LDR_LITERAL: {
            // Not used in lowering currently, but included for completeness
            AArch64_Register rt = instr->operands[0].reg;
            int32_t imm19 = (int32_t)instr->operands[1].immediate;
            // LDR (literal) 64-bit: opc=01, V=0, imm19
            uint32_t enc = 0x58000000 | (((uint32_t)imm19 & 0x7FFFF) << 5) | (uint32_t)rt;
            EMIT(enc);
            break;
        }
    }

    _UNIT_Dealloc(compile_context->context, instr);
    return _UNIT_OK;
error:
    _UNIT_Dealloc(compile_context->context, instr);
    return _UNIT_FAIL;

#undef EMIT
#undef INDEX
#undef EMIT_JUMP
}

// Prologue: stp x29, x30, [sp, #-(frame_size)]!
//           mov x29, sp
// This is 8 bytes (2 instructions).
// We reserve 8 bytes (2 NOP slots).
void
AArch64_PatchPrologue(_UNIT_CompileContext *compile_context,
                      UNIT_Size prologue_offset,
                      UNIT_Size frame_size)
{
    assert(compile_context != NULL);
    assert(prologue_offset >= 0);
    assert(frame_size >= 0);

    // frame_size includes the 16 bytes for FP/LR save
    if (frame_size == 0) {
        frame_size = 16; // minimum: just save fp/lr
    }

    // Ensure 16-byte alignment
    frame_size = (frame_size + 15) & ~15;

    // stp x29, x30, [sp, #-frame_size]!
    patch32(compile_context, prologue_offset,
            encode_stp_pre(REG_FP, REG_LR, REG_SP, -(int32_t)frame_size));

    // mov x29, sp  (ADD X29, SP, #0)
    patch32(compile_context, prologue_offset + 4,
            encode_add_imm(REG_FP, REG_SP, 0));
}

// Epilogue: mov sp, x29 (ADD SP, X29, #0)  -- not needed if we use ldp post-index
//           ldp x29, x30, [sp], #frame_size
// We also reserve 8 bytes (2 instructions).
void
AArch64_PatchEpilogue(_UNIT_CompileContext *compile_context,
                      UNIT_Size epilogue_offset,
                      UNIT_Size frame_size)
{
    assert(compile_context != NULL);
    assert(epilogue_offset >= 0);
    assert(frame_size >= 0);

    if (frame_size == 0) {
        frame_size = 16;
    }
    frame_size = (frame_size + 15) & ~15;

    // ldp x29, x30, [sp], #frame_size
    patch32(compile_context, epilogue_offset,
            encode_ldp_post(REG_FP, REG_LR, REG_SP, (int32_t)frame_size));

    // NOP for the second slot
    patch32(compile_context, epilogue_offset + 4, encode_nop());
}

void
AArch64_PatchJumps(_UNIT_CompileContext *compile_context)
{
    assert(compile_context != NULL);
    UNIT_Size count = _UNIT_Vector_SIZE(&compile_context->jump_table.pending_jumps);

    for (UNIT_Size index = 0; index < count; ++index) {
        _UNIT_PendingJump *jump = _UNIT_Vector_GET(&compile_context->jump_table.pending_jumps,
                                                   index);
        UNIT_Size label_offset = _UNIT_SizeMap_GET(&compile_context->jump_table.label_offsets,
                                                   jump->label_index);

        // The pending jump records the offset of the instruction.
        // On AArch64, the displacement is (target - pc) / 4.
        UNIT_Size pc = jump->patch_offset;
        int32_t displacement = (int32_t)(label_offset - pc) / 4;

        uint32_t original = read32(compile_context, pc);

        // Determine if this is a B or B.cond by checking the opcode bits
        if ((original & 0xFC000000) == 0x14000000) {
            // Unconditional B: imm26
            uint32_t patched = (original & 0xFC000000) | ((uint32_t)displacement & 0x03FFFFFF);
            patch32(compile_context, pc, patched);
        } else if ((original & 0xFF000010) == 0x54000000) {
            // B.cond: imm19
            uint32_t patched = (original & 0xFF00001F) | (((uint32_t)displacement & 0x7FFFF) << 5);
            patch32(compile_context, pc, patched);
        } else {
            // Should not happen
            _UNIT_Unreachable();
        }
    }
}