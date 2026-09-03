#include <sptl.h>

#include "ir.h"

typedef Sp_Hash_Table(Sp_String_View, Schwasm_Dispatcher *) Schwasm_Dispatch_Table;

static Schwasm_Dispatch_Table schwasm_dispatch_table = {0};

static inline long parse_hex_or_die(struct Schwasm *schwasm, const Sp_Lexer_Token *token) {
    errno = 0;
    char *endptr;
    long value = strtol(token->sv.ptr, &endptr, 16);

    if (errno != 0 || endptr - token->sv.ptr < (long) (token->sv.count + token->int_lit.suffixes.count)) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
        sp_die(1, SCHWASM_FILE_FMT " Error parsing hexadecimal value\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    return value;
}

enum GCPU_REG {
    GCPU_REGA,
    GCPU_REGB,
    GCPU_REGX,
    GCPU_REGY,
};

static inline void ld_ex(struct Schwasm *schwasm, enum GCPU_REG reg) {
    schwasm_expect_org(schwasm);

    const Sp_Lexer_Token *token = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *next = schwasm_peek_token(schwasm);

    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
    Sp_Lexer_Token_Line next_line = splexer_token_get_line(&schwasm->lexer, next);
    if (!next || token_line.line != next_line.line) {
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    enum Schwasm_Op op;
    long value = 0;
    bool deferred = false;
    sp_ht_node_t(&schwasm->label_table)* query = NULL;
    if (next->type == TOK_Pound) {   // immediate addressing
        schwasm_next_token(schwasm); // consume the TOK_Pound

        switch (reg) {
            case GCPU_REGA:
                op = SCHWASM_OP_LDAA_IMM;
                break;
            case GCPU_REGB:
                op = SCHWASM_OP_LDAB_IMM;
                break;
            case GCPU_REGX:
                op = SCHWASM_OP_LDX_IMM;
                break;
            case GCPU_REGY:
                op = SCHWASM_OP_LDY_IMM;
                break;
        }

        switch (schwasm_expect_value(schwasm)) {
            case SCHWASM_VALUE_HEX:
                value = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));
                break;
            case SCHWASM_VALUE_DECIMAL:
                value = (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
            case SCHWASM_VALUE_LABEL:
                token = schwasm_get_token(schwasm);
                sp_ht_get(&schwasm->label_table, token->sv, &query);

                if (!query) {
                    // TODO: dedicated function for label dependency
                    sp_ht_insert(&schwasm->label_table, token->sv, ((struct Schwasm_Label_Entry) { .defined = false }));
                    sp_ht_get(&schwasm->label_table, token->sv, &query);
                    assert(query);
                }

                if (!query->value.defined) deferred = true;
                else {
                    value = query->value.value;
                }
                break;
        }
        token_line = splexer_token_get_line(&schwasm->lexer, token);

        if (deferred) {
            // TODO: currently deferred node edits skip bounds checking
            schwasm_create_node(schwasm, op, (uint16_t) value);
            sp_da_push(&query->value.deferred_indices, schwasm->nodes.count - 1);
        } else {
            switch (reg) {
                case GCPU_REGA:
                case GCPU_REGB:
                    if (value > UINT8_MAX) {
                        sp_die(1, SCHWASM_FILE_FMT " Value too large; register is 8-bits\n", schwasm_file_arg(schwasm->filename, token_line));
                    }
                    break;
                case GCPU_REGX:
                case GCPU_REGY:
                    if (value > UINT16_MAX) {
                        sp_die(1, SCHWASM_FILE_FMT " Value too large; register is 16-bits\n", schwasm_file_arg(schwasm->filename, token_line));
                    }
                    break;
            }

            schwasm_create_node(schwasm, op, (uint16_t) value);
        }
    } else { // extended addressing
        switch (reg) {
            case GCPU_REGA:
                op = SCHWASM_OP_LDAA;
                break;
            case GCPU_REGB:
                op = SCHWASM_OP_LDAB;
                break;
            case GCPU_REGX:
                op = SCHWASM_OP_LDX;
                break;
            case GCPU_REGY:
                op = SCHWASM_OP_LDY;
                break;
        }

        switch (schwasm_expect_value(schwasm)) {
            case SCHWASM_VALUE_HEX:
                value = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));
                break;
            case SCHWASM_VALUE_DECIMAL:
                value = (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
            case SCHWASM_VALUE_LABEL:
                token = schwasm_get_token(schwasm);
                sp_ht_get(&schwasm->label_table, token->sv, &query);

                if (!query) {
                    // TODO: dedicated function for label dependency
                    sp_ht_insert(&schwasm->label_table, token->sv, ((struct Schwasm_Label_Entry) { .defined = false }));
                    sp_ht_get(&schwasm->label_table, token->sv, &query);
                    assert(query);
                }

                if (!query->value.defined) deferred = true;
                else {
                    value = query->value.value;
                }
                break;
        }

        next = schwasm_peek_token(schwasm);
        next_line = splexer_token_get_line(&schwasm->lexer, next);

        if (next && next->type == TOK_Comma && token_line.line == next_line.line) {
            schwasm_next_token(schwasm);

            next = schwasm_next_token(schwasm);
            next_line = splexer_token_get_line(&schwasm->lexer, next);
            if (!next || next->type != TOK_ID || next_line.line != token_line.line) {
                sp_die(1, SCHWASM_FILE_FMT " Expected address register for indexed addressing\n", schwasm_file_arg(schwasm->filename, token_line));
            }

            if (sp_sv_eq(&next->sv, &sp_cstr_slice("X"))) {
                switch (reg) {
                    case GCPU_REGA:
                        op = SCHWASM_OP_LDAA_X;
                        break;
                    case GCPU_REGB:
                        op = SCHWASM_OP_LDAB_X;
                        break;
                    default:
                        sp_die(1, SCHWASM_FILE_FMT " Indexed addressing not supported with 16-bit register\n", schwasm_file_arg(schwasm->filename, token_line));
                }
            } else if (sp_sv_eq(&next->sv, &sp_cstr_slice("Y"))) {
                switch (reg) {
                    case GCPU_REGA:
                        op = SCHWASM_OP_LDAA_Y;
                        break;
                    case GCPU_REGB:
                        op = SCHWASM_OP_LDAB_Y;
                        break;
                    default:
                        sp_die(1, SCHWASM_FILE_FMT " Indexed addressing not supported with 16-bit register\n", schwasm_file_arg(schwasm->filename, token_line));
                }
            } else {
                sp_die(1, SCHWASM_FILE_FMT " Unexpected address register \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, next_line), sp_sv_arg(next->sv));
            }

            if (deferred) {
                // TODO: currently deferred node edits skip bounds checking
                schwasm_create_node(schwasm, op, (uint16_t) value);
                sp_da_push(&query->value.deferred_indices, schwasm->nodes.count - 1);
            } else {
                if (value > UINT8_MAX) {
                    sp_die(1, SCHWASM_FILE_FMT " Value too large; 8-bit displacement expected\n", schwasm_file_arg(schwasm->filename, token_line));
                }

                schwasm_create_node(schwasm, op, (uint16_t) value);
            }

        } else {
            if (deferred) {
                // TODO: currently deferred node edits skip bounds checking
                schwasm_create_node(schwasm, op, (uint16_t) value);
                sp_da_push(&query->value.deferred_indices, schwasm->nodes.count - 1);
            } else {
                if (value > RAM_END) {
                    sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%04lX)\n", schwasm_file_arg(schwasm->filename, token_line), value);
                }

                schwasm_create_node(schwasm, op, (uint16_t) value);
            }
        }
    }
}
static inline void st_ex(struct Schwasm *schwasm, enum GCPU_REG reg) {
    schwasm_expect_org(schwasm);

    const Sp_Lexer_Token *token = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *next = schwasm_peek_token(schwasm);

    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
    Sp_Lexer_Token_Line next_line = splexer_token_get_line(&schwasm->lexer, next);
    if (!next || token_line.line != next_line.line) {
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    enum Schwasm_Op op;
    long addr = 0;
    bool deferred = false;
    sp_ht_node_t(&schwasm->label_table)* query = NULL;

    if (next->type == TOK_Pound) {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected immediate value; store operations cannot be performed onto immediate value\n", schwasm_file_arg(schwasm->filename, next_line));
    } else {
        switch (reg) {
            case GCPU_REGA:
                op = SCHWASM_OP_STAA;
                break;
            case GCPU_REGB:
                op = SCHWASM_OP_STAB;
                break;
            case GCPU_REGX:
            case GCPU_REGY:
                sp_unreachable();
                break;
        }

        switch (schwasm_expect_value(schwasm)) {
            case SCHWASM_VALUE_HEX:
                addr = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));
                break;
            case SCHWASM_VALUE_DECIMAL:
                addr = (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
            case SCHWASM_VALUE_LABEL:
                token = schwasm_get_token(schwasm);
                sp_ht_get(&schwasm->label_table, token->sv, &query);

                if (!query) {
                    // TODO: dedicated function for label dependency
                    sp_ht_insert(&schwasm->label_table, token->sv, ((struct Schwasm_Label_Entry) { .defined = false }));
                    sp_ht_get(&schwasm->label_table, token->sv, &query);
                    assert(query);
                }

                if (!query->value.defined) deferred = true;
                else {
                    addr = query->value.value;
                }
                break;
        }

        if (deferred) {
            // TODO: currently deferred node edits skip bounds checking
            schwasm_create_node(schwasm, op, (uint16_t) addr);
            sp_da_push(&query->value.deferred_indices, schwasm->nodes.count - 1);
        } else {
            if (addr > RAM_END) {
                token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%04lX)\n", schwasm_file_arg(schwasm->filename, token_line), addr);
            }

            schwasm_create_node(schwasm, op, (uint16_t) addr);
        }
    }
}
static inline void branch_ex(struct Schwasm *schwasm, enum Schwasm_Op op) {
    switch (op) {
        case SCHWASM_OP_BNE:
        case SCHWASM_OP_BEQ:
        case SCHWASM_OP_BN:
        case SCHWASM_OP_BP:
            break;
        default:
            sp_unreachable();
    }

    schwasm_expect_org(schwasm);

    const Sp_Lexer_Token *token = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *next = schwasm_peek_token(schwasm);

    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
    Sp_Lexer_Token_Line next_line = splexer_token_get_line(&schwasm->lexer, next);
    if (!next || token_line.line != next_line.line) {
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    long addr = 0;
    bool deferred = false;
    sp_ht_node_t(&schwasm->label_table)* query = NULL;

    if (next->type == TOK_Pound) {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected immediate value; branch operations require lower-order byte of addr\n", schwasm_file_arg(schwasm->filename, next_line));
    } else {
        switch (schwasm_expect_value(schwasm)) {
            case SCHWASM_VALUE_HEX:
                addr = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));
                break;
            case SCHWASM_VALUE_DECIMAL:
                addr = (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
            case SCHWASM_VALUE_LABEL:
                token = schwasm_get_token(schwasm);
                sp_ht_get(&schwasm->label_table, token->sv, &query);

                if (!query) {
                    // TODO: dedicated function for label dependency
                    sp_ht_insert(&schwasm->label_table, token->sv, ((struct Schwasm_Label_Entry) { .defined = false }));
                    sp_ht_get(&schwasm->label_table, token->sv, &query);
                    assert(query);
                }

                if (!query->value.defined) deferred = true;
                else {
                    addr = query->value.value;
                }
                break;
        }

        if (deferred) {
            // TODO: currently deferred node edits skip bounds checking
            schwasm_create_node(schwasm, op, (uint16_t) addr);
            sp_da_push(&query->value.deferred_indices, schwasm->nodes.count - 1);
        } else {
            if (addr > UINT8_MAX) {
                sp_die(1, SCHWASM_FILE_FMT " Cannot branch to (0x%04lX)\n", schwasm_file_arg(schwasm->filename, token_line), addr);
            }

            schwasm_create_node(schwasm, op, (uint16_t) addr);
        }
    }
}
enum Declare_Directive {
    DC_B,
    DS_B
};
// Preprocessor directives with a period in their identifier are tokenized as separate tokens by Splexer.
// As a downstream workaround, pointer contiguity is enforced below to disallow whitespace between DC/DS, '.', and 'B'.
static void d_ex(struct Schwasm *schwasm, enum Declare_Directive directive) {
    const Sp_Lexer_Token *prev = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *token = schwasm_next_token(schwasm);

    Sp_Lexer_Token_Line prev_line = splexer_token_get_line(&schwasm->lexer, prev);
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);

    if (!token || prev_line.line != token_line.line || token->type != TOK_Period || prev->sv.ptr + prev->sv.count != token->sv.ptr) {
        sp_die(1, SCHWASM_FILE_FMT " Failed to parse assembly directive \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, prev_line), sp_sv_arg(prev->sv));
    }

    prev = token;
    token = schwasm_next_token(schwasm);
    prev_line = token_line;
    token_line = splexer_token_get_line(&schwasm->lexer, token);

    if (!token || prev_line.line != token_line.line || !sp_sv_eq(&sp_cstr_slice("B"), &token->sv) || prev->sv.ptr + prev->sv.count != token->sv.ptr) {
        sp_die(1, SCHWASM_FILE_FMT " Failed to parse assembly directive\n", schwasm_file_arg(schwasm->filename, prev_line));
    }

    long value = 0;
    switch (directive) {
        case DC_B:
        dc_loop:
            switch (schwasm_expect_value(schwasm)) {
                case SCHWASM_VALUE_HEX:
                    if (schwasm_get_token(schwasm)->sv.count + schwasm_get_token(schwasm)->int_lit.suffixes.count > 2) {
                        token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
                        sp_die(1, SCHWASM_FILE_FMT " Value too large; ROM word size is 8-bit\n", schwasm_file_arg(schwasm->filename, token_line));
                    }
                    value = parse_hex_or_die(schwasm, schwasm_get_token(schwasm));
                    break;
                case SCHWASM_VALUE_DECIMAL:
                    value = schwasm_get_token(schwasm)->int_lit.value;
                    if (value > UINT8_MAX) {
                        token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
                        sp_die(1, SCHWASM_FILE_FMT " Value too large; ROM word size is 8-bit\n", schwasm_file_arg(schwasm->filename, token_line));
                    }
                    break;
                case SCHWASM_VALUE_LABEL:
                    // TODO: should support rhs labels
                    token_line = splexer_token_get_line(&schwasm->lexer, (token = schwasm_get_token(schwasm)));
                    sp_die(1, SCHWASM_FILE_FMT " DC.B does not support rhs labels\n", schwasm_file_arg(schwasm->filename, token_line));
                    break;
            }
            schwasm_create_node(schwasm, SCHWASM_AD_DC, (uint16_t) value);

            const Sp_Lexer_Token *next = schwasm_peek_token(schwasm);
            Sp_Lexer_Token_Line next_line = splexer_token_get_line(&schwasm->lexer, next);
            if (next && next->type == TOK_Comma && splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm)).line == next_line.line) {
                schwasm_next_token(schwasm);
                goto dc_loop;
            }
            break;
        case DS_B:
            switch (schwasm_expect_value(schwasm)) {
                case SCHWASM_VALUE_HEX:
                    value = parse_hex_or_die(schwasm, schwasm_get_token(schwasm));
                    break;
                case SCHWASM_VALUE_DECIMAL:
                    value = schwasm_get_token(schwasm)->int_lit.value;
                    break;
                case SCHWASM_VALUE_LABEL:
                    // TODO: should support rhs labels
                    token_line = splexer_token_get_line(&schwasm->lexer, (token = schwasm_get_token(schwasm)));
                    sp_die(1, SCHWASM_FILE_FMT " DS.B does not support rhs labels\n", schwasm_file_arg(schwasm->filename, token_line));
                    break;
            }
            token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
            if (schwasm->addr < 0x1000) {
                sp_die(1, SCHWASM_FILE_FMT " Cannot reserve bytes in read-only ROM (0x%04X)\n", schwasm_file_arg(schwasm->filename, token_line), schwasm->addr);
            }
            if (value > (long) ADDR_LEN - schwasm->addr) {
                sp_die(1, SCHWASM_FILE_FMT " Reserved address out of bounds (0x%04lX)\n", schwasm_file_arg(schwasm->filename, token_line), schwasm->addr + value);
            }
            if (value <= 0) {
                sp_die(1, SCHWASM_FILE_FMT " Cannot store 0 bytes\n", schwasm_file_arg(schwasm->filename, token_line));
            }
            schwasm_create_node(schwasm, SCHWASM_OP_UNKNOWN, (uint16_t) value);
            break;
    }
}

static void org(struct Schwasm *schwasm, void *data) {
    (void) data;
    long value = 0;
    const Sp_Lexer_Token *token;
    Sp_Lexer_Token_Line token_line;
    switch (schwasm_expect_value(schwasm)) {
        case SCHWASM_VALUE_HEX:
            value = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));

            if (token->sv.count + token->int_lit.suffixes.count > 4) { // exceeds 4 hex digits
                token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%lX)\n", schwasm_file_arg(schwasm->filename, token_line), value);
            }
            break;
        case SCHWASM_VALUE_DECIMAL:
            value = (token = schwasm_get_token(schwasm))->int_lit.value;
            if (value > RAM_END) {
                token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (%ld)\n", schwasm_file_arg(schwasm->filename, token_line), value);
            }
            break;
        case SCHWASM_VALUE_LABEL:
            token_line = splexer_token_get_line(&schwasm->lexer, (token = schwasm_get_token(schwasm)));
            sp_die(1, SCHWASM_FILE_FMT " ORG does not support rhs labels\n", schwasm_file_arg(schwasm->filename, token_line));
            break;
    }

    if (value < schwasm->addr) {
        schwasm->addr_unsorted = true;
    }
    schwasm->addr = (uint16_t) value;
    schwasm->addr_valid = true;
}
static void tab(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_TAB, 0);
}
static void tba(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_TBA, 0);
}
static void ldaa(struct Schwasm *schwasm, void *data) {
    (void) data;
    ld_ex(schwasm, GCPU_REGA);
}
static void ldab(struct Schwasm *schwasm, void *data) {
    (void) data;
    ld_ex(schwasm, GCPU_REGB);
}
static void ldx(struct Schwasm *schwasm, void *data) {
    (void) data;
    ld_ex(schwasm, GCPU_REGX);
}
static void ldy(struct Schwasm *schwasm, void *data) {
    (void) data;
    ld_ex(schwasm, GCPU_REGY);
}
static void staa(struct Schwasm *schwasm, void *data) {
    (void) data;
    st_ex(schwasm, GCPU_REGA);
}
static void stab(struct Schwasm *schwasm, void *data) {
    (void) data;
    st_ex(schwasm, GCPU_REGB);
}
static void sum_ba(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_SUM_BA, 0);
}
static void sum_ab(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_SUM_AB, 0);
}
static void and_ba(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_AND_BA, 0);
}
static void and_ab(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_AND_AB, 0);
}
static void or_ba(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_OR_BA, 0);
}
static void or_ab(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_OR_AB, 0);
}
static void coma(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_COMA, 0);
}
static void comb(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_COMB, 0);
}
static void shfa_l(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_SHFA_L, 0);
}
static void shfa_r(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_SHFA_R, 0);
}
static void shfb_l(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_SHFB_L, 0);
}
static void shfb_r(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_SHFB_R, 0);
}
static void beq(struct Schwasm *schwasm, void *data) {
    (void) data;
    branch_ex(schwasm, SCHWASM_OP_BEQ);
}
static void bne(struct Schwasm *schwasm, void *data) {
    (void) data;
    branch_ex(schwasm, SCHWASM_OP_BNE);
}
static void bn(struct Schwasm *schwasm, void *data) {
    (void) data;
    branch_ex(schwasm, SCHWASM_OP_BN);
}
static void bp(struct Schwasm *schwasm, void *data) {
    (void) data;
    branch_ex(schwasm, SCHWASM_OP_BP);
}
static void inx(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_INX, 0);
}
static void iny(struct Schwasm *schwasm, void *data) {
    (void) data;
    schwasm_create_node(schwasm, SCHWASM_OP_INY, 0);
}
static void equ(struct Schwasm *schwasm, void *data) {
    if (!data) {
        // no lhs label was found
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " EQU requires a label definition\n", schwasm_file_arg(schwasm->filename, token_line));
    }
    const Sp_String_View *label = (const Sp_String_View *) data;

    long value;
    const Sp_Lexer_Token *token;
    Sp_Lexer_Token_Line token_line;
    switch (schwasm_expect_value(schwasm)) {
        case SCHWASM_VALUE_HEX:
            value = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));
            break;
        case SCHWASM_VALUE_DECIMAL:
            value = (token = schwasm_get_token(schwasm))->int_lit.value;
            break;
        case SCHWASM_VALUE_LABEL:
            token_line = splexer_token_get_line(&schwasm->lexer, (token = schwasm_get_token(schwasm)));
            sp_die(1, SCHWASM_FILE_FMT " EQU does not support rhs labels\n", schwasm_file_arg(schwasm->filename, token_line));
            break;
    }

    // TODO: dedicated function for label definitions
    sp_ht_node_t(&schwasm->label_table)* query = NULL;
    sp_ht_get(&schwasm->label_table, *label, &query);

    if (query) {
        if (query->value.defined) {
            token_line = splexer_token_get_line(&schwasm->lexer, token);
            sp_die(1, SCHWASM_FILE_FMT " Cannot redefine label \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, token_line), sp_sv_arg(*label));
        }

        for (size_t i = 0; i < query->value.deferred_indices.count; ++i) {
            schwasm_node_edit(&schwasm->nodes.data[query->value.deferred_indices.data[i]], (uint16_t) value);
        }
        sp_da_free(&query->value.deferred_indices);
        query->value.deferred_indices.count = 0;
        query->value.deferred_indices.capacity = 0;
        query->value.defined = true;
        query->value.value = value;
    } else {
        sp_ht_insert(&schwasm->label_table, *label, ((struct Schwasm_Label_Entry) {.defined = true, .value = value}));
    }
}
static void dc_b(struct Schwasm *schwasm, void *data) {
    (void) data;
    d_ex(schwasm, DC_B);
}
static void ds_b(struct Schwasm *schwasm, void *data) {
    (void) data;
    d_ex(schwasm, DS_B);
}

__attribute__((constructor)) static void schwasm_dispatch_table_constructor(void) {
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("ORG"), &org);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("TAB"), &tab);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("TBA"), &tba);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("LDAA"), &ldaa);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("LDAB"), &ldab);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("LDX"), &ldx);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("LDY"), &ldy);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("STAA"), &staa);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("STAB"), &stab);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("SUM_BA"), &sum_ba);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("SUM_AB"), &sum_ab);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("AND_BA"), &and_ba);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("AND_AB"), &and_ab);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("OR_BA"), &or_ba);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("OR_AB"), &or_ab);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("COMA"), &coma);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("COMB"), &comb);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("SHFA_L"), &shfa_l);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("SHFA_R"), &shfa_r);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("SHFB_L"), &shfb_l);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("SHFB_R"), &shfb_r);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("BEQ"), &beq);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("BNE"), &bne);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("BN"), &bn);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("BP"), &bp);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("INX"), &inx);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("INY"), &iny);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("DC"), &dc_b);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("DS"), &ds_b);
    sp_ht_insert(&schwasm_dispatch_table, sp_cstr_slice("EQU"), &equ);
}

__attribute__((destructor)) void schwasm_dispatcher_destructor(void) {
    sp_ht_free(&schwasm_dispatch_table);
}

Schwasm_Dispatcher *schwasm_dispatcher_get(const char *str) {
    return schwasm_dispatcher_get_sv(sp_cstr_slice(str));
}

inline Schwasm_Dispatcher *schwasm_dispatcher_get_sv(Sp_String_View sv) {
    sp_ht_node_t(&schwasm_dispatch_table) *query = NULL;

    sp_ht_get(&schwasm_dispatch_table, sv, &query);

    if (!query) {
        return NULL;
    }

    return query->value;
}

