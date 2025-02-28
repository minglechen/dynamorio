/* **********************************************************
 * Copyright (c) 2015-2022 Google, Inc.  All rights reserved.
 * Copyright (c) 2022 ARM Limited. All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of ARM Limited nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL ARM LIMITED OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Define DR_FAST_IR to verify that everything compiles when we call the inline
 * versions of these routines.
 */
#ifndef STANDALONE_DECODER
#    define DR_FAST_IR 1
#endif

/* Uses the DR API, using DR as a standalone library, rather than
 * being a client library working with DR on a target program.
 */

#include "configure.h"
#include "dr_api.h"
#include "tools.h"

static byte buf[8192];

reg_id_t Q_registers[31] = { DR_REG_Q1,  DR_REG_Q2,  DR_REG_Q3,  DR_REG_Q4,  DR_REG_Q5,
                             DR_REG_Q6,  DR_REG_Q7,  DR_REG_Q8,  DR_REG_Q9,  DR_REG_Q10,
                             DR_REG_Q11, DR_REG_Q12, DR_REG_Q13, DR_REG_Q14, DR_REG_Q15,
                             DR_REG_Q16, DR_REG_Q17, DR_REG_Q18, DR_REG_Q19, DR_REG_Q21,
                             DR_REG_Q22, DR_REG_Q23, DR_REG_Q24, DR_REG_Q25, DR_REG_Q26,
                             DR_REG_Q27, DR_REG_Q28, DR_REG_Q29, DR_REG_Q30, DR_REG_Q31 };

reg_id_t D_registers[31] = { DR_REG_D1,  DR_REG_D2,  DR_REG_D3,  DR_REG_D4,  DR_REG_D5,
                             DR_REG_D6,  DR_REG_D7,  DR_REG_D8,  DR_REG_D9,  DR_REG_D10,
                             DR_REG_D11, DR_REG_D12, DR_REG_D13, DR_REG_D14, DR_REG_D15,
                             DR_REG_D16, DR_REG_D17, DR_REG_D18, DR_REG_D19, DR_REG_D21,
                             DR_REG_D22, DR_REG_D23, DR_REG_D24, DR_REG_D25, DR_REG_D26,
                             DR_REG_D27, DR_REG_D28, DR_REG_D29, DR_REG_D30, DR_REG_D31 };

#ifdef STANDALONE_DECODER
#    define ASSERT(x)                                                                 \
        ((void)((!(x)) ? (fprintf(stderr, "ASSERT FAILURE (standalone): %s:%d: %s\n", \
                                  __FILE__, __LINE__, #x),                            \
                          abort(), 0)                                                 \
                       : 0))
#else
#    define ASSERT(x)                                                                \
        ((void)((!(x)) ? (dr_fprintf(STDERR, "ASSERT FAILURE (client): %s:%d: %s\n", \
                                     __FILE__, __LINE__, #x),                        \
                          dr_abort(), 0)                                             \
                       : 0))
#endif

#define TEST_INSTR(instruction_name) bool test_instr_##instruction_name(void *dc)

#define RUN_INSTR_TEST(instruction_name)                   \
    test_result = test_instr_##instruction_name(dcontext); \
    if (test_result == false) {                            \
        print("test for " #instruction_name " failed.\n"); \
        result = false;                                    \
    }

static bool
test_instr_encoding(void *dc, uint opcode, instr_t *instr, const char *expected)
{
    instr_t *decin;
    byte *pc;
    char *end, *big;
    size_t len = strlen(expected);
    size_t buflen = (len < 100 ? 100 : len) + 2;
    bool result = true;
    char *buf = malloc(buflen);

    if (instr_get_opcode(instr) != opcode) {
        print("incorrect opcode for instr %s: %s", opcode, instr_get_opcode(instr));
        result = false;
    }
    instr_disassemble_to_buffer(dc, instr, buf, buflen);
    end = buf + strlen(buf);
    if (end > buf && *(end - 1) == '\n')
        --end;

    if (end - buf != len || memcmp(buf, expected, len) != 0) {
        print("dissassembled as:\n");
        print("   %s\n", buf);
        print("but expected:\n");
        print("   %s\n", expected);
        result = false;
    }

    if (!instr_is_encoding_possible(instr)) {
        print("encoding for expected %s not possible\n", expected);
        result = false;
    } else {
        pc = instr_encode(dc, instr, buf);
        decin = instr_create(dc);
        decode(dc, buf, decin);
        if (!instr_same(instr, decin)) {
            print("Reencoding failed, dissassembled as:\n   ");
            instr_disassemble(dc, decin, STDERR);
            print("\n");
            print("but expected:\n");
            print("   %s\n", expected);
            result = false;
        }
    }
    instr_destroy(dc, instr);
    instr_destroy(dc, decin);

    return result;
}

TEST_INSTR(sqrdmlsh_vector)
{
    bool success = true;
    instr_t *instr;
    byte *pc;

    opnd_t elsz;
    reg_id_t Rd_0[3] = { DR_REG_D0, DR_REG_D10, DR_REG_D31 };
    reg_id_t Rn_0[3] = { DR_REG_D0, DR_REG_D10, DR_REG_D31 };
    reg_id_t Rm_0[3] = { DR_REG_D0, DR_REG_D10, DR_REG_D31 };
    elsz = OPND_CREATE_HALF();
    const char *expected_0[3] = {
        "sqrdmlsh %d0 %d0 %d0 $0x01 -> %d0",
        "sqrdmlsh %d10 %d10 %d10 $0x01 -> %d10",
        "sqrdmlsh %d31 %d31 %d31 $0x01 -> %d31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_vector(dc, opnd_create_reg(Rd_0[i]),
                                             opnd_create_reg(Rn_0[i]),
                                             opnd_create_reg(Rm_0[i]), elsz);
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_0[i]))
            success = false;
    }

    reg_id_t Rd_1[3] = { DR_REG_D0, DR_REG_D10, DR_REG_D31 };
    reg_id_t Rn_1[3] = { DR_REG_D0, DR_REG_D10, DR_REG_D31 };
    reg_id_t Rm_1[3] = { DR_REG_D0, DR_REG_D10, DR_REG_D31 };
    elsz = OPND_CREATE_SINGLE();
    const char *expected_1[3] = {
        "sqrdmlsh %d0 %d0 %d0 $0x02 -> %d0",
        "sqrdmlsh %d10 %d10 %d10 $0x02 -> %d10",
        "sqrdmlsh %d31 %d31 %d31 $0x02 -> %d31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_vector(dc, opnd_create_reg(Rd_1[i]),
                                             opnd_create_reg(Rn_1[i]),
                                             opnd_create_reg(Rm_1[i]), elsz);
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_1[i]))
            success = false;
    }

    reg_id_t Rd_2[3] = { DR_REG_Q0, DR_REG_Q10, DR_REG_Q31 };
    reg_id_t Rn_2[3] = { DR_REG_Q0, DR_REG_Q10, DR_REG_Q31 };
    reg_id_t Rm_2[3] = { DR_REG_Q0, DR_REG_Q10, DR_REG_Q31 };
    elsz = OPND_CREATE_HALF();
    const char *expected_2[3] = {
        "sqrdmlsh %q0 %q0 %q0 $0x01 -> %q0",
        "sqrdmlsh %q10 %q10 %q10 $0x01 -> %q10",
        "sqrdmlsh %q31 %q31 %q31 $0x01 -> %q31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_vector(dc, opnd_create_reg(Rd_2[i]),
                                             opnd_create_reg(Rn_2[i]),
                                             opnd_create_reg(Rm_2[i]), elsz);
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_2[i]))
            success = false;
    }

    reg_id_t Rd_3[3] = { DR_REG_Q0, DR_REG_Q10, DR_REG_Q31 };
    reg_id_t Rn_3[3] = { DR_REG_Q0, DR_REG_Q10, DR_REG_Q31 };
    reg_id_t Rm_3[3] = { DR_REG_Q0, DR_REG_Q10, DR_REG_Q31 };
    elsz = OPND_CREATE_SINGLE();
    const char *expected_3[3] = {
        "sqrdmlsh %q0 %q0 %q0 $0x02 -> %q0",
        "sqrdmlsh %q10 %q10 %q10 $0x02 -> %q10",
        "sqrdmlsh %q31 %q31 %q31 $0x02 -> %q31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_vector(dc, opnd_create_reg(Rd_3[i]),
                                             opnd_create_reg(Rn_3[i]),
                                             opnd_create_reg(Rm_3[i]), elsz);
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_3[i]))
            success = false;
    }

    return success;
}

TEST_INSTR(sqrdmlsh_scalar_idx)
{
    bool success = true;
    instr_t *instr;
    byte *pc;

    opnd_t elsz;
    reg_id_t Rd_0[3] = { DR_REG_H0, DR_REG_H10, DR_REG_H31 };
    reg_id_t Rn_0[3] = { DR_REG_H0, DR_REG_H10, DR_REG_H31 };
    reg_id_t Rm_0[3] = { DR_REG_Q0, DR_REG_Q5, DR_REG_Q15 };
    uint index_0[3] = { 0, 2, 7 };
    elsz = OPND_CREATE_HALF();
    const char *expected_0[3] = {
        "sqrdmlsh %h0 %h0 %q0 $0x00 $0x01 -> %h0",
        "sqrdmlsh %h10 %h10 %q5 $0x02 $0x01 -> %h10",
        "sqrdmlsh %h31 %h31 %q15 $0x07 $0x01 -> %h31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_scalar_idx(
            dc, opnd_create_reg(Rd_0[i]), opnd_create_reg(Rn_0[i]),
            opnd_create_reg(Rm_0[i]), opnd_create_immed_uint(index_0[i], OPSZ_0), elsz);
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_0[i]))
            success = false;
    }

    reg_id_t Rd_1[3] = { DR_REG_S0, DR_REG_S10, DR_REG_S31 };
    reg_id_t Rn_1[3] = { DR_REG_S0, DR_REG_S10, DR_REG_S31 };
    reg_id_t Rm_1[3] = { DR_REG_Q0, DR_REG_Q5, DR_REG_Q15 };
    uint index_1[3] = { 0, 1, 3 };
    elsz = OPND_CREATE_SINGLE();
    const char *expected_1[3] = {
        "sqrdmlsh %s0 %s0 %q0 $0x00 $0x02 -> %s0",
        "sqrdmlsh %s10 %s10 %q5 $0x01 $0x02 -> %s10",
        "sqrdmlsh %s31 %s31 %q15 $0x03 $0x02 -> %s31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_scalar_idx(
            dc, opnd_create_reg(Rd_1[i]), opnd_create_reg(Rn_1[i]),
            opnd_create_reg(Rm_1[i]), opnd_create_immed_uint(index_1[i], OPSZ_0), elsz);
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_1[i]))
            success = false;
    }

    return success;
}

TEST_INSTR(sqrdmlsh_scalar)
{
    bool success = true;
    instr_t *instr;
    byte *pc;

    reg_id_t Rd_0[3] = { DR_REG_H0, DR_REG_H10, DR_REG_H31 };
    reg_id_t Rn_0[3] = { DR_REG_H0, DR_REG_H10, DR_REG_H31 };
    reg_id_t Rm_0[3] = { DR_REG_H0, DR_REG_H10, DR_REG_H31 };
    const char *expected_0[3] = {
        "sqrdmlsh %h0 %h0 %h0 -> %h0",
        "sqrdmlsh %h10 %h10 %h10 -> %h10",
        "sqrdmlsh %h31 %h31 %h31 -> %h31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_scalar(dc, opnd_create_reg(Rd_0[i]),
                                             opnd_create_reg(Rn_0[i]),
                                             opnd_create_reg(Rm_0[i]));
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_0[i]))
            success = false;
    }

    reg_id_t Rd_1[3] = { DR_REG_S0, DR_REG_S10, DR_REG_S31 };
    reg_id_t Rn_1[3] = { DR_REG_S0, DR_REG_S10, DR_REG_S31 };
    reg_id_t Rm_1[3] = { DR_REG_S0, DR_REG_S10, DR_REG_S31 };
    const char *expected_1[3] = {
        "sqrdmlsh %s0 %s0 %s0 -> %s0",
        "sqrdmlsh %s10 %s10 %s10 -> %s10",
        "sqrdmlsh %s31 %s31 %s31 -> %s31",
    };
    for (int i = 0; i < 3; i++) {
        instr = INSTR_CREATE_sqrdmlsh_scalar(dc, opnd_create_reg(Rd_1[i]),
                                             opnd_create_reg(Rn_1[i]),
                                             opnd_create_reg(Rm_1[i]));
        if (!test_instr_encoding(dc, OP_sqrdmlsh, instr, expected_1[i]))
            success = false;
    }

    return success;
}

int
main(int argc, char *argv[])
{
#ifdef STANDALONE_DECODER
    void *dcontext = GLOBAL_DCONTEXT;
#else
    void *dcontext = dr_standalone_init();
#endif
    bool result = true;
    bool test_result;

    RUN_INSTR_TEST(sqrdmlsh_scalar);
    RUN_INSTR_TEST(sqrdmlsh_scalar_idx);
    RUN_INSTR_TEST(sqrdmlsh_vector);

    print("All v8.1 tests complete.\n");
#ifndef STANDALONE_DECODER
    dr_standalone_exit();
#endif
    if (result)
        return 0;
    return 1;
}
