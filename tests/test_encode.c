// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/x86_encode.h"
#include "nybit/target_x86_64.h"
#include "nybit/regalloc.h"
#include "nybit/opt.h"
#include "nybit/parser.h"
#include <string.h>

static void assert_bytes_match(const uint8_t *actual, size_t actual_len, const uint8_t *expected, size_t expected_len) {
    TEST_ASSERT_EQ(actual_len, expected_len);
    for (size_t i = 0; i < expected_len; i++) {
        TEST_ASSERT_EQ(actual[i], expected[i]);
    }
}

void test_encode_mov_instructions(void) {
    X86_Code_Buffer buf;
    x86_buf_init(&buf);

    /* mov rax, rbx -> 48 89 d8 */
    X86_Instruction inst1 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_reg(x86_reg_phys(X86_RBX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst1, nullptr));
    const uint8_t exp1[] = { 0x48, 0x89, 0xD8 };
    assert_bytes_match(buf.bytes, buf.count, exp1, sizeof(exp1));

    /* mov r10, r11 -> 4d 89 da */
    buf.count = 0;
    X86_Instruction inst2 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_R10, 8)), x86_op_reg(x86_reg_phys(X86_R11, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst2, nullptr));
    const uint8_t exp2[] = { 0x4D, 0x89, 0xDA };
    assert_bytes_match(buf.bytes, buf.count, exp2, sizeof(exp2));

    /* mov eax, ebx -> 89 d8 */
    buf.count = 0;
    X86_Instruction inst3 = {
        .opcode = X86_OPC_MOV,
        .size = 4,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 4)), x86_op_reg(x86_reg_phys(X86_RBX, 4)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst3, nullptr));
    const uint8_t exp3[] = { 0x89, 0xD8 };
    assert_bytes_match(buf.bytes, buf.count, exp3, sizeof(exp3));

    /* mov sil, dil -> 40 88 fe */
    buf.count = 0;
    X86_Instruction inst4 = {
        .opcode = X86_OPC_MOV,
        .size = 1,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RSI, 1)), x86_op_reg(x86_reg_phys(X86_RDI, 1)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst4, nullptr));
    const uint8_t exp4[] = { 0x40, 0x88, 0xFE };
    assert_bytes_match(buf.bytes, buf.count, exp4, sizeof(exp4));

    /* mov rax, [rbp - 8] -> 48 8b 45 f8 */
    buf.count = 0;
    X86_Mem m1 = { .base = x86_reg_phys(X86_RBP, 8), .disp = -8 };
    X86_Instruction inst5 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_mem(m1) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst5, nullptr));
    const uint8_t exp5[] = { 0x48, 0x8B, 0x45, 0xF8 };
    assert_bytes_match(buf.bytes, buf.count, exp5, sizeof(exp5));

    /* mov [rbp - 16], rdx -> 48 89 55 f0 */
    buf.count = 0;
    X86_Mem m2 = { .base = x86_reg_phys(X86_RBP, 8), .disp = -16 };
    X86_Instruction inst6 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_mem(m2), x86_op_reg(x86_reg_phys(X86_RDX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst6, nullptr));
    const uint8_t exp6[] = { 0x48, 0x89, 0x55, 0xF0 };
    assert_bytes_match(buf.bytes, buf.count, exp6, sizeof(exp6));

    /* mov [rsp], rax -> 48 89 04 24 */
    buf.count = 0;
    X86_Mem m3 = { .base = x86_reg_phys(X86_RSP, 8), .disp = 0 };
    X86_Instruction inst7 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_mem(m3), x86_op_reg(x86_reg_phys(X86_RAX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst7, nullptr));
    const uint8_t exp7[] = { 0x48, 0x89, 0x04, 0x24 };
    assert_bytes_match(buf.bytes, buf.count, exp7, sizeof(exp7));

    /* mov rax, [rdi + rsi*4 + 8] -> 48 8b 44 b7 08 */
    buf.count = 0;
    X86_Mem m4 = {
        .base = x86_reg_phys(X86_RDI, 8),
        .index = x86_reg_phys(X86_RSI, 8),
        .scale = 4,
        .disp = 8,
    };
    X86_Instruction inst8 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_mem(m4) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst8, nullptr));
    const uint8_t exp8[] = { 0x48, 0x8B, 0x44, 0xB7, 0x08 };
    assert_bytes_match(buf.bytes, buf.count, exp8, sizeof(exp8));

    /* mov rax, 42 -> 48 c7 c0 2a 00 00 00 */
    buf.count = 0;
    X86_Instruction inst9 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_imm(42) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst9, nullptr));
    const uint8_t exp9[] = { 0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00 };
    assert_bytes_match(buf.bytes, buf.count, exp9, sizeof(exp9));

    /* mov eax, 42 -> b8 2a 00 00 00 */
    buf.count = 0;
    X86_Instruction inst10 = {
        .opcode = X86_OPC_MOV,
        .size = 4,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 4)), x86_op_imm(42) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst10, nullptr));
    const uint8_t exp10[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00 };
    assert_bytes_match(buf.bytes, buf.count, exp10, sizeof(exp10));

    /* mov rax, 0x1122334455667788LL -> 48 b8 88 77 66 55 44 33 22 11 */
    buf.count = 0;
    X86_Instruction inst11 = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_imm(0x1122334455667788LL) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst11, nullptr));
    const uint8_t exp11[] = { 0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
    assert_bytes_match(buf.bytes, buf.count, exp11, sizeof(exp11));

    x86_buf_destroy(&buf);
}

void test_encode_arithmetic_instructions(void) {
    X86_Code_Buffer buf;
    x86_buf_init(&buf);

    /* add rax, rbx -> 48 01 d8 */
    X86_Instruction inst1 = {
        .opcode = X86_OPC_ADD,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_reg(x86_reg_phys(X86_RBX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst1, nullptr));
    const uint8_t exp1[] = { 0x48, 0x01, 0xD8 };
    assert_bytes_match(buf.bytes, buf.count, exp1, sizeof(exp1));

    /* add rax, 1 -> 48 83 c0 01 */
    buf.count = 0;
    X86_Instruction inst2 = {
        .opcode = X86_OPC_ADD,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_imm(1) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst2, nullptr));
    const uint8_t exp2[] = { 0x48, 0x83, 0xC0, 0x01 };
    assert_bytes_match(buf.bytes, buf.count, exp2, sizeof(exp2));

    /* sub rsp, 16 -> 48 83 ec 10 */
    buf.count = 0;
    X86_Instruction inst3 = {
        .opcode = X86_OPC_SUB,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RSP, 8)), x86_op_imm(16) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst3, nullptr));
    const uint8_t exp3[] = { 0x48, 0x83, 0xEC, 0x10 };
    assert_bytes_match(buf.bytes, buf.count, exp3, sizeof(exp3));

    /* sub r10, r11 -> 4d 29 da */
    buf.count = 0;
    X86_Instruction inst4 = {
        .opcode = X86_OPC_SUB,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_R10, 8)), x86_op_reg(x86_reg_phys(X86_R11, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst4, nullptr));
    const uint8_t exp4[] = { 0x4D, 0x29, 0xDA };
    assert_bytes_match(buf.bytes, buf.count, exp4, sizeof(exp4));

    /* xor eax, eax -> 31 c0 */
    buf.count = 0;
    X86_Instruction inst5 = {
        .opcode = X86_OPC_XOR,
        .size = 4,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 4)), x86_op_reg(x86_reg_phys(X86_RAX, 4)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst5, nullptr));
    const uint8_t exp5[] = { 0x31, 0xC0 };
    assert_bytes_match(buf.bytes, buf.count, exp5, sizeof(exp5));

    /* imul rax, rbx -> 48 0f af c3 */
    buf.count = 0;
    X86_Instruction inst6 = {
        .opcode = X86_OPC_IMUL,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_reg(x86_reg_phys(X86_RBX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst6, nullptr));
    const uint8_t exp6[] = { 0x48, 0x0F, 0xAF, 0xC3 };
    assert_bytes_match(buf.bytes, buf.count, exp6, sizeof(exp6));

    /* idiv rcx -> 48 f7 f9 */
    buf.count = 0;
    X86_Instruction inst7 = {
        .opcode = X86_OPC_IDIV,
        .size = 8,
        .op_count = 1,
        .ops = { x86_op_reg(x86_reg_phys(X86_RCX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst7, nullptr));
    const uint8_t exp7[] = { 0x48, 0xF7, 0xF9 };
    assert_bytes_match(buf.bytes, buf.count, exp7, sizeof(exp7));

    /* neg rax -> 48 f7 d8 */
    buf.count = 0;
    X86_Instruction inst8 = {
        .opcode = X86_OPC_NEG,
        .size = 8,
        .op_count = 1,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst8, nullptr));
    const uint8_t exp8[] = { 0x48, 0xF7, 0xD8 };
    assert_bytes_match(buf.bytes, buf.count, exp8, sizeof(exp8));

    /* shl rax, 4 -> 48 c1 e0 04 */
    buf.count = 0;
    X86_Instruction inst9 = {
        .opcode = X86_OPC_SHL,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_imm(4) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst9, nullptr));
    const uint8_t exp9[] = { 0x48, 0xC1, 0xE0, 0x04 };
    assert_bytes_match(buf.bytes, buf.count, exp9, sizeof(exp9));

    /* sete al -> 0f 94 c0 */
    buf.count = 0;
    X86_Instruction inst10 = {
        .opcode = X86_OPC_SETCC,
        .size = 1,
        .cond = X86_COND_E,
        .op_count = 1,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 1)) }
    };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst10, nullptr));
    const uint8_t exp10[] = { 0x0F, 0x94, 0xC0 };
    assert_bytes_match(buf.bytes, buf.count, exp10, sizeof(exp10));

    /* push rbp -> 55, pop rbp -> 5d */
    buf.count = 0;
    X86_Instruction p_rbp = { .opcode = X86_OPC_PUSH, .size = 8, .op_count = 1, .ops = { x86_op_reg(x86_reg_phys(X86_RBP, 8)) } };
    X86_Instruction po_rbp = { .opcode = X86_OPC_POP, .size = 8, .op_count = 1, .ops = { x86_op_reg(x86_reg_phys(X86_RBP, 8)) } };
    TEST_ASSERT(x86_encode_instruction(&buf, &p_rbp, nullptr));
    TEST_ASSERT(x86_encode_instruction(&buf, &po_rbp, nullptr));
    const uint8_t exp_rbp[] = { 0x55, 0x5D };
    assert_bytes_match(buf.bytes, buf.count, exp_rbp, sizeof(exp_rbp));

    /* push r12 -> 41 54, pop r12 -> 41 5c */
    buf.count = 0;
    X86_Instruction p_r12 = { .opcode = X86_OPC_PUSH, .size = 8, .op_count = 1, .ops = { x86_op_reg(x86_reg_phys(X86_R12, 8)) } };
    X86_Instruction po_r12 = { .opcode = X86_OPC_POP, .size = 8, .op_count = 1, .ops = { x86_op_reg(x86_reg_phys(X86_R12, 8)) } };
    TEST_ASSERT(x86_encode_instruction(&buf, &p_r12, nullptr));
    TEST_ASSERT(x86_encode_instruction(&buf, &po_r12, nullptr));
    const uint8_t exp_r12[] = { 0x41, 0x54, 0x41, 0x5C };
    assert_bytes_match(buf.bytes, buf.count, exp_r12, sizeof(exp_r12));

    /* cdq -> 99, cqo -> 48 99, ret -> c3, ud2 -> 0f 0b */
    buf.count = 0;
    X86_Instruction inst_cdq = { .opcode = X86_OPC_CDQ, .op_count = 0 };
    X86_Instruction inst_cqo = { .opcode = X86_OPC_CQO, .op_count = 0 };
    X86_Instruction inst_ret = { .opcode = X86_OPC_RET, .op_count = 0 };
    X86_Instruction inst_ud2 = { .opcode = X86_OPC_UD2, .op_count = 0 };
    TEST_ASSERT(x86_encode_instruction(&buf, &inst_cdq, nullptr));
    TEST_ASSERT(x86_encode_instruction(&buf, &inst_cqo, nullptr));
    TEST_ASSERT(x86_encode_instruction(&buf, &inst_ret, nullptr));
    TEST_ASSERT(x86_encode_instruction(&buf, &inst_ud2, nullptr));
    const uint8_t exp_misc[] = { 0x99, 0x48, 0x99, 0xC3, 0x0F, 0x0B };
    assert_bytes_match(buf.bytes, buf.count, exp_misc, sizeof(exp_misc));

    x86_buf_destroy(&buf);
}

void test_encode_branch_fixup_resolution(void) {
    X86_Function fn;
    x86_func_init(&fn, ny_str("test_branch"), NY_ABI_SYSV_AMD64);

    fn.block_count = 2;
    fn.block_capacity = 2;
    fn.blocks = (X86_Block *)ny_alloc_zero(2 * sizeof(X86_Block));

    /* Block 0: ID = 0 */
    fn.blocks[0].id = 0;
    fn.blocks[0].name = ny_str("entry");

    /* cmp rdi, 0 -> 48 83 ff 00 (4 bytes) */
    X86_Instruction cmp_inst = {
        .opcode = X86_OPC_CMP,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RDI, 8)), x86_op_imm(0) }
    };
    x86_block_append_inst(&fn.blocks[0], cmp_inst);

    /* je .exit -> 0f 84 [disp32] (6 bytes) */
    X86_Instruction je_inst = {
        .opcode = X86_OPC_JCC,
        .size = 8,
        .cond = X86_COND_E,
        .op_count = 1,
        .ops = { x86_op_label(1) }
    };
    x86_block_append_inst(&fn.blocks[0], je_inst);

    /* mov rax, 1 -> 48 c7 c0 01 00 00 00 (7 bytes) */
    X86_Instruction mov_inst = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_phys(X86_RAX, 8)), x86_op_imm(1) }
    };
    x86_block_append_inst(&fn.blocks[0], mov_inst);

    /* jmp .exit -> e9 [disp32] (5 bytes) */
    X86_Instruction jmp_inst = {
        .opcode = X86_OPC_JMP,
        .size = 8,
        .op_count = 1,
        .ops = { x86_op_label(1) }
    };
    x86_block_append_inst(&fn.blocks[0], jmp_inst);

    /* Block 1: ID = 1 */
    fn.blocks[1].id = 1;
    fn.blocks[1].name = ny_str("exit");

    /* ret -> c3 (1 byte) */
    X86_Instruction ret_inst = {
        .opcode = X86_OPC_RET,
        .size = 8,
        .op_count = 0
    };
    x86_block_append_inst(&fn.blocks[1], ret_inst);

    X86_Code_Buffer buf;
    x86_buf_init(&buf);

    TEST_ASSERT(x86_encode_function(&buf, &fn, nullptr));

    /* Expected layout:
       0x00: cmp rdi, 0       (4 bytes: 48 83 ff 00)
       0x04: je .exit          (6 bytes: 0f 84 disp32) -> next inst is at 0x0A
       0x0A: mov rax, 1        (7 bytes: 48 c7 c0 01 00 00 00)
       0x11: jmp .exit         (5 bytes: e9 disp32)   -> next inst is at 0x16
       0x16: ret               (1 byte:  c3) -> target offset is 0x16
       
       je disp32  = 0x16 - 0x0A = 0x0C (12)
       jmp disp32 = 0x16 - 0x16 = 0x00 (0)
    */
    TEST_ASSERT_EQ(buf.count, 0x17);
    TEST_ASSERT_EQ(buf.bytes[0x04], 0x0F);
    TEST_ASSERT_EQ(buf.bytes[0x05], 0x84);
    int32_t je_disp = (int32_t)(buf.bytes[0x06] | (buf.bytes[0x07] << 8) | (buf.bytes[0x08] << 16) | (buf.bytes[0x09] << 24));
    TEST_ASSERT_EQ(je_disp, 12);

    TEST_ASSERT_EQ(buf.bytes[0x11], 0xE9);
    int32_t jmp_disp = (int32_t)(buf.bytes[0x12] | (buf.bytes[0x13] << 8) | (buf.bytes[0x14] << 16) | (buf.bytes[0x15] << 24));
    TEST_ASSERT_EQ(jmp_disp, 0);

    TEST_ASSERT_EQ(buf.bytes[0x16], 0xC3);

    x86_buf_destroy(&buf);
    x86_func_destroy(&fn);
}

void test_encode_module_and_relocations(void) {
    X86_Module mod;
    x86_mod_init(&mod, ny_str("test_mod"), NY_ABI_SYSV_AMD64);

    mod.function_count = 2;
    mod.function_capacity = 2;
    mod.functions = (X86_Function *)ny_alloc_zero(2 * sizeof(X86_Function));

    x86_func_init(&mod.functions[0], ny_str("caller"), NY_ABI_SYSV_AMD64);
    mod.functions[0].block_count = 1;
    mod.functions[0].block_capacity = 1;
    mod.functions[0].blocks = (X86_Block *)ny_alloc_zero(sizeof(X86_Block));
    mod.functions[0].blocks[0].id = 0;

    /* call @callee (intra-module) */
    X86_Instruction call_local = {
        .opcode = X86_OPC_CALL,
        .size = 8,
        .op_count = 1,
        .ops = { x86_op_global(ny_str("callee")) }
    };
    x86_block_append_inst(&mod.functions[0].blocks[0], call_local);

    /* call @ext_printf (external) */
    X86_Instruction call_ext = {
        .opcode = X86_OPC_CALL,
        .size = 8,
        .op_count = 1,
        .ops = { x86_op_global(ny_str("ext_printf")) }
    };
    x86_block_append_inst(&mod.functions[0].blocks[0], call_ext);

    X86_Instruction ret1 = { .opcode = X86_OPC_RET, .op_count = 0 };
    x86_block_append_inst(&mod.functions[0].blocks[0], ret1);

    x86_func_init(&mod.functions[1], ny_str("callee"), NY_ABI_SYSV_AMD64);
    mod.functions[1].block_count = 1;
    mod.functions[1].block_capacity = 1;
    mod.functions[1].blocks = (X86_Block *)ny_alloc_zero(sizeof(X86_Block));
    mod.functions[1].blocks[0].id = 0;

    X86_Instruction ret2 = { .opcode = X86_OPC_RET, .op_count = 0 };
    x86_block_append_inst(&mod.functions[1].blocks[0], ret2);

    X86_Encoded_Module emod;
    TEST_ASSERT(x86_encode_module(&emod, &mod, nullptr));

    TEST_ASSERT_EQ(emod.function_count, 2);
    TEST_ASSERT_EQ(emod.functions[0].offset, 0);
    TEST_ASSERT_EQ(emod.functions[0].size, 11); /* e8 disp32 (5) + e8 disp32 (5) + c3 (1) */
    TEST_ASSERT_EQ(emod.functions[1].offset, 11);
    TEST_ASSERT_EQ(emod.functions[1].size, 1);  /* c3 (1) */

    /* The call @callee should be resolved: offset 0 + 5 = 5. Target is 11. Disp = 11 - 5 = 6. */
    int32_t call_disp = (int32_t)(emod.text_section.bytes[1] |
                                  (emod.text_section.bytes[2] << 8) |
                                  (emod.text_section.bytes[3] << 16) |
                                  (emod.text_section.bytes[4] << 24));
    TEST_ASSERT_EQ(call_disp, 6);

    /* The call @ext_printf cannot be resolved intra-module, so it must be in relocs */
    TEST_ASSERT_EQ(emod.text_section.reloc_count, 1);
    TEST_ASSERT_STR_EQ(emod.text_section.relocs[0].symbol_name.data, "ext_printf");
    TEST_ASSERT_EQ(emod.text_section.relocs[0].code_offset, 6);

    x86_encoded_mod_destroy(&emod);
    x86_mod_destroy(&mod);
}

void test_encode_validation(void) {
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    /* Virtual register in physical instruction should fail */
    X86_Instruction bad_vreg = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_reg(x86_reg_virt(99, 8)), x86_op_reg(x86_reg_phys(X86_RAX, 8)) }
    };
    X86_Code_Buffer buf;
    x86_buf_init(&buf);

    TEST_ASSERT(!x86_encode_instruction(&buf, &bad_vreg, &diags));
    TEST_ASSERT(diags.count > 0);

    /* Mem-to-mem operation should fail */
    X86_Mem m = { .base = x86_reg_phys(X86_RBP, 8), .disp = -8 };
    X86_Instruction bad_mem2mem = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .op_count = 2,
        .ops = { x86_op_mem(m), x86_op_mem(m) }
    };
    TEST_ASSERT(!x86_encode_instruction(&buf, &bad_mem2mem, &diags));

    x86_buf_destroy(&buf);
    ny_diagnostic_list_destroy(&diags);
}

void test_encode_e2e_full_pipeline(void) {
    const char *src =
        "@function fib(%n: i64) -> i64;\n"
        ".entry;\n"
        "    %two = const 2;\n"
        "    %cond = cmp.lt.s %n, %two;\n"
        "    @branch_if %cond, .base, .recurse;\n"
        ".base;\n"
        "    @return %n;\n"
        ".recurse;\n"
        "    %c1 = const 1;\n"
        "    %n1 = sub %n, %c1;\n"
        "    %r1 = call @fib, %n1;\n"
        "    %c2 = const 2;\n"
        "    %n2 = sub %n, %c2;\n"
        "    %r2 = call @fib, %n2;\n"
        "    %sum = add %r1, %r2;\n"
        "    @return %sum;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_e2e_encode");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool parse_ok = ny_parse_module(&p);
    TEST_ASSERT(parse_ok);

    bool opt_ok = ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O2);
    (void)opt_ok;

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(mir_ok);
    TEST_ASSERT_EQ(diags.count, 0);

    for (size_t i = 0; i < mmod.function_count; i++) {
        Ny_RegAlloc_Result res;
        bool ra_ok = ny_regalloc_run(&mmod.functions[i], NY_ABI_SYSV_AMD64, &res, &diags);
        TEST_ASSERT(ra_ok);
        TEST_ASSERT_EQ(diags.count, 0);

        bool val_ok = ny_mfunc_validate_allocated(&mmod.functions[i], &diags);
        TEST_ASSERT(val_ok);
        TEST_ASSERT_EQ(diags.count, 0);
    }

    X86_Module xmod;
    bool x86_ok = x86_lower_machine_mod(&g_ny_target_x86_64_sysv, &mmod, &xmod, &diags);
    TEST_ASSERT(x86_ok);
    TEST_ASSERT_EQ(diags.count, 0);

    X86_Encoded_Module emod;
    bool enc_ok = x86_encode_module(&emod, &xmod, &diags);
    TEST_ASSERT(enc_ok);
    TEST_ASSERT_EQ(diags.count, 0);

    /* Verify non-empty byte buffer produced */
    TEST_ASSERT(emod.text_section.count > 0);
    TEST_ASSERT_EQ(emod.function_count, 1);
    TEST_ASSERT(ny_str_eq_cstr(emod.functions[0].name, "fib"));

    /* Prologue: push rbp (0x55) */
    TEST_ASSERT_EQ(emod.text_section.bytes[0], 0x55);

    /* Epilogue has ret (0xC3) */
    TEST_ASSERT_EQ(emod.text_section.bytes[emod.text_section.count - 1], 0xC3);

    x86_encoded_mod_destroy(&emod);
    x86_mod_destroy(&xmod);
    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}
