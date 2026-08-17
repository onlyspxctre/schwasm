#include <errno.h>
#include <splexer.h>
#include <sptl.h>

#define ROM_SIZE 4096

#define SCHWASM_FILE_FMT "%s:%ld:%ld:"
#define schwasm_file_arg(name, tok_line) name, tok_line.line, tok_line.col

enum Schwasm_Op {
    SCHWASM_OP_TAB = 0x00,
    SCHWASM_OP_TBA = 0x01,
    SCHWASM_OP_LDAA_IMM = 0x02,
    SCHWASM_OP_LDAB_IMM = 0x03,
    SCHWASM_OP_LDAA = 0x04,
    SCHWASM_OP_LDAB = 0x05,
    SCHWASM_OP_STAA = 0x06,
    SCHWASM_OP_STAB = 0x07,
    SCHWASM_OP_LDX_IMM = 0x08,
    SCHWASM_OP_LDY_IMM = 0x09,
    SCHWASM_OP_LDX = 0x0A,
    SCHWASM_OP_LDY = 0x0B,
    SCHWASM_OP_LDAA_X = 0x0C,
    SCHWASM_OP_LDAA_Y = 0x0D,
    SCHWASM_OP_LDAB_X = 0x0E,
    SCHWASM_OP_LDAB_Y = 0x0F,
    SCHWASM_OP_STAA_X = 0x10,
    SCHWASM_OP_STAA_Y = 0x11,
    SCHWASM_OP_STAB_X = 0x12,
    SCHWASM_OP_STAB_Y = 0x13,
    SCHWASM_OP_SUM_BA = 0x14,
    SCHWASM_OP_SUM_AB = 0x15,
    SCHWASM_OP_AND_BA = 0x16,
    SCHWASM_OP_AND_AB = 0x17,
    SCHWASM_OP_OR_BA = 0x18,
    SCHWASM_OP_OR_AB = 0x19,
    SCHWASM_OP_COMA = 0x1A,
    SCHWASM_OP_COMB = 0x1B,
    SCHWASM_OP_SHFA_L = 0x1C,
    SCHWASM_OP_SHFA_R = 0x1D,
    SCHWASM_OP_SHFB_L = 0x1E,
    SCHWASM_OP_SHFB_R = 0x1F,
    SCHWASM_OP_BEQ = 0x20,
    SCHWASM_OP_BNE = 0x21,
    SCHWASM_OP_BN = 0x22,
    SCHWASM_OP_BP = 0x23,
    SCHWASM_OP_INX = 0x30,
    SCHWASM_OP_INY = 0x31,
    SCHWASM_AD_DC = 0x24,
    SCHWASM_OP_UNKNOWN = 0x25,
};

static uint8_t SCHWASM_OP_COUNT[] = {
    [SCHWASM_OP_TAB] = 1,
    [SCHWASM_OP_TBA] = 1,
    [SCHWASM_OP_LDAA_IMM] = 2,
    [SCHWASM_OP_LDAB_IMM] = 2,
    [SCHWASM_OP_LDAA] = 3,
    [SCHWASM_OP_LDAB] = 3,
    [SCHWASM_OP_STAA] = 3,
    [SCHWASM_OP_STAB] = 3,
    [SCHWASM_OP_LDX_IMM] = 3,
    [SCHWASM_OP_LDY_IMM] = 3,
    [SCHWASM_OP_LDX] = 3,
    [SCHWASM_OP_LDY] = 3,
    [SCHWASM_OP_LDAA_X] = 2,
    [SCHWASM_OP_LDAA_Y] = 2,
    [SCHWASM_OP_LDAB_X] = 2,
    [SCHWASM_OP_LDAB_Y] = 2,
    [SCHWASM_OP_STAA_X] = 2,
    [SCHWASM_OP_STAA_Y] = 2,
    [SCHWASM_OP_STAB_X] = 2,
    [SCHWASM_OP_STAB_Y] = 2,
    [SCHWASM_OP_SUM_BA] = 1,
    [SCHWASM_OP_SUM_AB] = 1,
    [SCHWASM_OP_AND_BA] = 1,
    [SCHWASM_OP_AND_AB] = 1,
    [SCHWASM_OP_OR_BA] = 1,
    [SCHWASM_OP_OR_AB] = 1,
    [SCHWASM_OP_COMA] = 1,
    [SCHWASM_OP_COMB] = 1,
    [SCHWASM_OP_SHFA_L] = 1,
    [SCHWASM_OP_SHFA_R] = 1,
    [SCHWASM_OP_SHFB_L] = 1,
    [SCHWASM_OP_SHFB_R] = 1,
    [SCHWASM_OP_BEQ] = 2,
    [SCHWASM_OP_BNE] = 2,
    [SCHWASM_OP_BN] = 2,
    [SCHWASM_OP_BP] = 2,
    [SCHWASM_OP_INX] = 1,
    [SCHWASM_OP_INY] = 1,
    [SCHWASM_AD_DC] = 1,
    [SCHWASM_OP_UNKNOWN] = 0,
};

struct Schwasm_Node {
    enum Schwasm_Op op;
    union {
        uint8_t word;
        struct {
            uint8_t lword;
            uint8_t hword;
        };
        uint16_t dword;
    };
    uint16_t addr;
};

int schwasm_node_lesser_cmp(const struct Schwasm_Node lhs, const struct Schwasm_Node rhs) {
    return lhs.addr < rhs.addr;
}

struct Schwasm {
    Sp_Lexer lexer;
    Sp_Heap(struct Schwasm_Node) nodes;
    Sp_Bitset used_addrs;
    struct {
        size_t idx;
        bool busy;
    } lexer_attr;
    uint16_t addr; // GCPU uses a 4K ROM, only requires 12-bit wide address; 16-bits is more than enough
    bool addr_valid;
    const char *filename;
};

static inline struct Schwasm schwasm_init(const char *filename) {
    struct Schwasm schwasm = {
        .nodes = {
            .cmp = &schwasm_node_lesser_cmp,
        },
        .filename = filename,
        .addr = UINT16_MAX,
    };

    return schwasm;
}

static inline const Sp_Lexer_Token *schwasm_get_token(struct Schwasm *schwasm);

static inline void schwasm_expect_org(struct Schwasm *schwasm) {
    if (!schwasm->addr_valid) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " No preceding ORG to define initial address\n", schwasm_file_arg(schwasm->filename, token_line));
    } else if (schwasm->addr >= ROM_SIZE) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " Address out of bounds\n", schwasm_file_arg(schwasm->filename, token_line));
    }
}

static inline void schwasm_create_node(struct Schwasm *schwasm, enum Schwasm_Op op, uint16_t dword) {
    schwasm_expect_org(schwasm);

    struct Schwasm_Node node = (struct Schwasm_Node) {
        .op = op,
        .addr = schwasm->addr,
    };

    // Address collision check
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
    uint16_t count = (op == SCHWASM_OP_UNKNOWN) ? dword : SCHWASM_OP_COUNT[op]; // if SCHWASM_OP_UNKNOWN we are doing DS
    for (uint16_t i = 0; i < count; ++i) {
        if (sp_bitset_check(&schwasm->used_addrs, schwasm->addr)) {
            // TODO: make this error msg elaborate on what address it is colliding with
            sp_die(1, SCHWASM_FILE_FMT " Address collision\n", schwasm_file_arg(schwasm->filename, token_line));
        }

        sp_bitset_set(&schwasm->used_addrs, schwasm->addr);
        ++schwasm->addr;
    }

    switch (SCHWASM_OP_COUNT[op]) {
        case 0:
            node.dword = dword;
            break;
        case 1:
        case 2:
            node.word = (uint8_t) dword;
            break;
        case 3:
            node.lword = (uint8_t) dword;
            node.hword = (uint8_t) (dword >> 8);
            break;
        default:
            sp_unreachable();
    }

    sp_heap_push(&schwasm->nodes, node);
}

static inline const Sp_Lexer_Token *schwasm_get_token(struct Schwasm *schwasm) {
    if (schwasm->lexer_attr.idx >= schwasm->lexer.tokens.count) {
        return NULL;
    }
    return &schwasm->lexer.tokens.data[schwasm->lexer_attr.idx];
}

static inline const Sp_Lexer_Token *schwasm_peek_token(struct Schwasm *schwasm) {
    if (schwasm->lexer_attr.idx + 1 >= schwasm->lexer.tokens.count) {
        return NULL;
    }

    return &schwasm->lexer.tokens.data[schwasm->lexer_attr.idx + 1];
}

static inline const Sp_Lexer_Token *schwasm_next_token(struct Schwasm *schwasm) {
    ++schwasm->lexer_attr.idx;
    return schwasm_get_token(schwasm);
}

enum Schwasm_Value_Type {
    SCHWASM_VALUE_HEX,
    SCHWASM_VALUE_DECIMAL,
};
static inline enum Schwasm_Value_Type schwasm_expect_value(struct Schwasm *schwasm) {
    const Sp_Lexer_Token *prev = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *token = schwasm_next_token(schwasm);

    Sp_Lexer_Token_Line prev_line = splexer_token_get_line(&schwasm->lexer, prev);
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
    if (!token || prev_line.line != token_line.line) {
        prev_line.col += prev->sv.count - 1; // points col to the end of the previous token
        sp_die(1, SCHWASM_FILE_FMT " Expected value\n", schwasm_file_arg(schwasm->filename, prev_line));
    }

    if (sp_sv_eq(&sp_cstr_slice("$"), &token->sv)) {
        prev = schwasm_get_token(schwasm);
        token = schwasm_next_token(schwasm);

        prev_line = splexer_token_get_line(&schwasm->lexer, prev);
        token_line = splexer_token_get_line(&schwasm->lexer, token);

        if (!token) {
            sp_die(1, SCHWASM_FILE_FMT " Unexpected EOF\n", schwasm_file_arg(schwasm->filename, prev_line));
        }

        if (prev_line.line != token_line.line) {
            sp_die(1, SCHWASM_FILE_FMT " Expected hexadecimal value after $\n", schwasm_file_arg(schwasm->filename, prev_line));
        }

        if (token->type != TOK_IntLiteral && token->type != TOK_ID) {
            sp_die(1, SCHWASM_FILE_FMT " Unexpected token after $\n", schwasm_file_arg(schwasm->filename, prev_line));
        }

        return SCHWASM_VALUE_HEX;
    } else if (token->type == TOK_IntLiteral) {
        return SCHWASM_VALUE_DECIMAL;
    } else {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected value\n", schwasm_file_arg(schwasm->filename, prev_line));
        return 1;
    }
}

static inline void schwasm_destroy(struct Schwasm *schwasm) {
    splexer_destroy(&schwasm->lexer);
    sp_bitset_free(&schwasm->used_addrs);
    sp_heap_free(&schwasm->nodes);
}

static inline int parse_hex_or_die(struct Schwasm *schwasm, const Sp_Lexer_Token *token) {
    errno = 0;
    char *endptr;
    int value = (int) strtol(token->sv.ptr, &endptr, 16);

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

void org(struct Schwasm *schwasm) {
    int value;
    const Sp_Lexer_Token *token;
    switch (schwasm_expect_value(schwasm)) {
        case SCHWASM_VALUE_HEX:
            value = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));

            if (token->sv.count + token->int_lit.suffixes.count > 4) { // exceeds 4 hex digits
                Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%X) \n", schwasm_file_arg(schwasm->filename, token_line), value);
            }

            schwasm->addr = (uint16_t) value;
            break;
        case SCHWASM_VALUE_DECIMAL:
            value = (int) (token = schwasm_get_token(schwasm))->int_lit.value;
            if (value > UINT16_MAX) {
                Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (%d) \n", schwasm_file_arg(schwasm->filename, token_line), value);
            }

            schwasm->addr = (uint16_t) value;
            break;
    }

    schwasm->addr_valid = true;
}


void ld(struct Schwasm *schwasm, enum GCPU_REG reg) {
    schwasm_expect_org(schwasm);

    const Sp_Lexer_Token *token = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *next =  schwasm_peek_token(schwasm);

    if (!next) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    enum Schwasm_Op op;
    int value;
    if (next->type == TOK_Pound) { // immediate addressing
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
                value = (int) (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
        }

        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);

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
                value = (int) (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
        }

        if (value >= ROM_SIZE) {
            Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
            sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%04X)\n", schwasm_file_arg(schwasm->filename, token_line), value);
        }

        schwasm_create_node(schwasm, op, (uint16_t) value);
    }
}

void st(struct Schwasm *schwasm, enum GCPU_REG reg) {
    schwasm_expect_org(schwasm);

    const Sp_Lexer_Token *token = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *next =  schwasm_peek_token(schwasm);

    if (!next) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    enum Schwasm_Op op;
    int addr;

    if (next->type == TOK_Pound) {
        Sp_Lexer_Token_Line next_line = splexer_token_get_line(&schwasm->lexer, next);
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
                addr = (int) (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
        }

        if (addr >= ROM_SIZE) {
            Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
            sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%04X)\n", schwasm_file_arg(schwasm->filename, token_line), addr);
        }

        schwasm_create_node(schwasm, op, (uint16_t) addr);
    }
}

void branch(struct Schwasm *schwasm, enum Schwasm_Op op) {
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
    const Sp_Lexer_Token *next =  schwasm_peek_token(schwasm);

    if (!next) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    int addr;
    if (next->type == TOK_Pound) {
        Sp_Lexer_Token_Line next_line = splexer_token_get_line(&schwasm->lexer, next);
        sp_die(1, SCHWASM_FILE_FMT " Unexpected immediate value; branch operations require lower-order byte of addr\n", schwasm_file_arg(schwasm->filename, next_line));
    } else {
        switch (schwasm_expect_value(schwasm)) {
            case SCHWASM_VALUE_HEX:
                addr = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));
                break;
            case SCHWASM_VALUE_DECIMAL:
                addr = (int) (token = schwasm_get_token(schwasm))->int_lit.value;
                break;
        }

        if (addr >= UINT8_MAX) {
            Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
            sp_die(1, SCHWASM_FILE_FMT " Cannot branch to (0x%04X)\n", schwasm_file_arg(schwasm->filename, token_line), addr);
        }

        schwasm_create_node(schwasm, op, (uint16_t) addr);
    }
}

enum Declare_Directive {
    DC_B,
    DS_B
};

// TODO: Preprocessor directives with a period in their identifier is currently parsed as separate tokens.
// This is because Splexer currently does not support periods in identifiers.
// As a result, this means that something like "DC   .   B" with whitespace in the middle of the directive is completely valid.
// We must fix this on the Splexer side; we will probably implement a compile flag that enables periods within identifier names.
void declare_directive(struct Schwasm *schwasm, enum Declare_Directive directive) {
    const Sp_Lexer_Token *prev = schwasm_get_token(schwasm);
    const Sp_Lexer_Token *token = schwasm_next_token(schwasm);

    Sp_Lexer_Token_Line prev_line = splexer_token_get_line(&schwasm->lexer, prev);
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);

    if (!token || prev_line.line != token_line.line || token->type != TOK_Period) {
        sp_die(1, SCHWASM_FILE_FMT " Failed to parse assembly directive \"" SP_SV_FMT "\"", schwasm_file_arg(schwasm->filename, prev_line), sp_sv_arg(prev->sv));
    }

    prev = token;
    token = schwasm_next_token(schwasm);
    prev_line = token_line;
    token_line = splexer_token_get_line(&schwasm->lexer, token);

    if (!token || prev_line.line != token_line.line || !sp_sv_eq(&sp_cstr_slice("B"), &token->sv)) {
        sp_die(1, SCHWASM_FILE_FMT " Failed to parse assembly directive", schwasm_file_arg(schwasm->filename, prev_line));
    }

    int value;
    switch (directive) {
        case DC_B:
        dc_loop:
            switch (schwasm_expect_value(schwasm)) {
                case SCHWASM_VALUE_HEX:
                    if (schwasm_get_token(schwasm)->sv.count + schwasm_get_token(schwasm)->int_lit.suffixes.count > 2) {
                        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
                        sp_die(1, SCHWASM_FILE_FMT " Value too large; ROM word size is 8-bit\n", schwasm_file_arg(schwasm->filename, token_line));
                    }
                    value = parse_hex_or_die(schwasm, schwasm_get_token(schwasm));
                    break;
                case SCHWASM_VALUE_DECIMAL:
                    value = (int) schwasm_get_token(schwasm)->int_lit.value;
                    if (value > UINT8_MAX) {
                        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
                        sp_die(1, SCHWASM_FILE_FMT " Value too large; ROM word size is 8-bit\n", schwasm_file_arg(schwasm->filename, token_line));
                    }
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
                    value = (int) schwasm_get_token(schwasm)->int_lit.value;
                    break;
            }
            // TODO: need a bound on value
            schwasm_create_node(schwasm, SCHWASM_OP_UNKNOWN, (uint16_t) value);
            break;
    }
}

static Sp_String_Builder generated = {0};
static struct Schwasm schwasm = {0};

void cleanup() {
    sp_da_free(&generated);
    schwasm_destroy(&schwasm);
}

int main(int argc, char **argv) {
    if (argc == 1) {
        sp_die(1, "Not enough arguments (expected a filename)\n");
    }
    if (argc > 2) {
        sp_die(1, "Too many arguments (expected 1, got %d)\n", argc - 1);
    }

    schwasm = schwasm_init(argv[1]);
    atexit(cleanup);

    if (splexer_init(&schwasm.lexer, argv[1]) != 0) {
        sp_die(1, "Unable to open file: \"%s\"\n", argv[1]);
    }

    Sp_Lexer_Return_Code code;
    while ((code = splexer_tokenize(&schwasm.lexer)) == SPLEXER_OK)
        ;

    if (code == SPLEXER_ERROR) {
        if (schwasm.lexer.state == SPLEXER_STATE_MULTICOMMENT) {
            sp_die(1, "C-style multi-line comments are not supported!\n");
        } else {
            sp_die(1, "Unknown tokenizer error occurred!\n");
        }
    }

    sp_sb_appendf(&generated, "-- \tGenerated by Schwasm.\t --\n");
    sp_sb_appendf(&generated, "DEPTH = %d;\n", ROM_SIZE);
    sp_sb_appendf(&generated, "WIDTH = 8;\n\n");

    sp_sb_appendf(&generated, "ADDRESS_RADIX = HEX;\n");
    sp_sb_appendf(&generated, "DATA_RADIX = HEX;\n\n");

    sp_sb_appendf(&generated, "CONTENT\nBEGIN\n\n");

    const Sp_Lexer_Token *prev = NULL;
    const Sp_Lexer_Token *token = schwasm_get_token(&schwasm);
    Sp_Lexer_Token_Line prev_line = {0};
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm.lexer, token);

    if (!token) {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected initial token\n", schwasm_file_arg(schwasm.filename, ((Sp_Lexer_Token_Line) {
                                                                                                         .line = 1,
                                                                                                         .col = 1,
                                                                                                     })));
    }

    do {
        // prev_line is calculated at the end of previous iteration
        token_line = splexer_token_get_line(&schwasm.lexer, token);
        if (prev && prev_line.line != token_line.line) {
            schwasm.lexer_attr.busy = false;
        } else if (schwasm.lexer_attr.busy) { // some token after a completed instruction
            sp_die(1, SCHWASM_FILE_FMT " Trailing tokens after instruction\n", schwasm_file_arg(schwasm.filename, token_line));
        }

        if (token->type != TOK_ID) {
            sp_die(1,
                   SCHWASM_FILE_FMT " Failed to parse unknown symbol \"%s\"\n",
                   schwasm_file_arg(schwasm.filename, token_line),
                   SPLEXER_TOKENS_LITERAL[token->type]);
        }

        schwasm.lexer_attr.busy = true;

        if (sp_sv_eq(&sp_cstr_slice("ORG"), &token->sv)) {
            org(&schwasm);
        } else if (sp_sv_eq(&sp_cstr_slice("TAB"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_TAB, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("TBA"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_TBA, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("LDAA"), &token->sv)) {
            ld(&schwasm, GCPU_REGA);
        } else if (sp_sv_eq(&sp_cstr_slice("LDAB"), &token->sv)) {
            ld(&schwasm, GCPU_REGB);
        } else if (sp_sv_eq(&sp_cstr_slice("LDX"), &token->sv)) {
            ld(&schwasm, GCPU_REGX);
        } else if (sp_sv_eq(&sp_cstr_slice("LDY"), &token->sv)) {
            ld(&schwasm, GCPU_REGY);
        } else if (sp_sv_eq(&sp_cstr_slice("STAA"), &token->sv)) {
            st(&schwasm, GCPU_REGA);
        } else if (sp_sv_eq(&sp_cstr_slice("STAB"), &token->sv)) {
            st(&schwasm, GCPU_REGB);
        } else if (sp_sv_eq(&sp_cstr_slice("SUM_BA"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_SUM_BA, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("SUM_AB"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_SUM_AB, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("AND_BA"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_AND_BA, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("AND_AB"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_AND_AB, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("OR_BA"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_OR_BA, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("OR_AB"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_OR_AB, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("COMA"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_COMA, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("COMB"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_COMB, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFA_L"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_SHFA_L, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFA_R"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_SHFA_R, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFB_L"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_SHFB_L, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFB_R"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_SHFB_R, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("BNE"), &token->sv)) {
            branch(&schwasm, SCHWASM_OP_BNE);
        } else if (sp_sv_eq(&sp_cstr_slice("BEQ"), &token->sv)) {
            branch(&schwasm, SCHWASM_OP_BEQ);
        } else if (sp_sv_eq(&sp_cstr_slice("BN"), &token->sv)) {
            branch(&schwasm, SCHWASM_OP_BN);
        } else if (sp_sv_eq(&sp_cstr_slice("BP"), &token->sv)) {
            branch(&schwasm, SCHWASM_OP_BP);
        } else if (sp_sv_eq(&sp_cstr_slice("INX"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_INX, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("INY"), &token->sv)) {
            schwasm_create_node(&schwasm, SCHWASM_OP_INY, 0);
        } else if (sp_sv_eq(&sp_cstr_slice("DC"), &token->sv)) {
            declare_directive(&schwasm, DC_B);
        } else if (sp_sv_eq(&sp_cstr_slice("DS"), &token->sv)) {
            declare_directive(&schwasm, DS_B);
        } else {
            sp_die(1, SCHWASM_FILE_FMT " Failed to parse unknown instruction \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm.filename, token_line), sp_sv_arg(token->sv));
        }

        prev = schwasm_get_token(&schwasm);
        prev_line = splexer_token_get_line(&schwasm.lexer, prev);
    } while ((token = schwasm_next_token(&schwasm)));

    uint16_t next_addr = 0x0000;
    struct Schwasm_Node node;

    while (schwasm.nodes.count > 0) {
        node = sp_heap_top(&schwasm.nodes);

        // "calloc" unspecified memory regions
        if (node.addr > next_addr) {
            sp_sb_appendf(&generated, "[%04X..%04X]\t:\t%02X;\n\n", next_addr, node.addr - 1, 0x00);
        }

        next_addr = node.addr + SCHWASM_OP_COUNT[node.op];

        switch (SCHWASM_OP_COUNT[node.op]) {
            case 0:
                sp_sb_appendf(&generated, "[%04X..%04X]\t:\t%02X;\n\n", node.addr, node.addr + node.dword - 1, 0x00); // allocate node.dword bytes
                next_addr = node.addr + node.dword;
                break;
            case 1:
                if (node.op == SCHWASM_AD_DC) {
                    sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n\n", node.addr, node.word);
                } else {
                    sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n\n", node.addr, node.op);
                }
                break;
            case 2:
                sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n", node.addr, node.op);
                sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n\n", node.addr + 1, node.word);
                break;
            case 3:
                sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n", node.addr, node.op);
                sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n", node.addr + 1, node.lword);
                sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n\n", node.addr + 2, node.hword);
                break;
            default:
                sp_unreachable();
        }
        

        sp_heap_pop(&schwasm.nodes);
    }

    // "calloc" until end
    if (0x0FFF > next_addr) {
        sp_sb_appendf(&generated, "[%04X..%04X]\t:\t%02X;\n", next_addr, 0x0FFF, 0x00);
    }

    sp_sb_appendf(&generated, "END;\n");
    printf("%s", generated.data);
    return 0;
}
