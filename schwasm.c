#include <errno.h>
#include <math.h>
#include <splexer.h>
#include <sptl.h>

#define ROM_SIZE 4096

#define SCHWASM_FILE_FMT "%s:%ld:%ld:"
#define schwasm_file_arg(name, tok_line) name, tok_line.line, tok_line.col

// TODO: instead of generating dumb nodes, we should generate abstract syntax.
// This will allow us to decouple the interpreter side from the IR/machine-code generator.
struct Schwasm_Node {
    uint16_t addr;
    uint8_t code;
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
    if (schwasm->addr == UINT16_MAX) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " No preceding ORG to define initial address\n", schwasm_file_arg(schwasm->filename, token_line));
    } else if (schwasm->addr >= ROM_SIZE) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " Address out of bounds\n", schwasm_file_arg(schwasm->filename, token_line));
    }
}

static inline void schwasm_create_node(struct Schwasm *schwasm, uint8_t code) {
    schwasm_expect_org(schwasm);
    if (sp_bitset_check(&schwasm->used_addrs, schwasm->addr)) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " Address collision\n", schwasm_file_arg(schwasm->filename, token_line));
    }
    sp_heap_push(&schwasm->nodes, ((struct Schwasm_Node) {.addr = schwasm->addr, .code = code}));
    sp_bitset_set(&schwasm->used_addrs, schwasm->addr);
    ++schwasm->addr;
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
            // TODO: decimal check if out of range
            value = (int) (token = schwasm_get_token(schwasm))->int_lit.value;
            if (value > UINT16_MAX) {
                Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (%d) \n", schwasm_file_arg(schwasm->filename, token_line), value);
            }

            schwasm->addr = (uint16_t) value;
            break;
    }
}

void ld_imm(struct Schwasm *schwasm, enum GCPU_REG reg) {
    switch (reg) {
        case GCPU_REGA:
            schwasm_create_node(schwasm, 0x02);
            break;
        case GCPU_REGB:
            schwasm_create_node(schwasm, 0x03);
            break;
        case GCPU_REGX:
            schwasm_create_node(schwasm, 0x08);
            break;
        case GCPU_REGY:
            schwasm_create_node(schwasm, 0x09);
            break;
    }

    const Sp_Lexer_Token *token;
    int value;

    switch (schwasm_expect_value(schwasm)) {
        case SCHWASM_VALUE_HEX:
            value = parse_hex_or_die(schwasm, (token = schwasm_get_token(schwasm)));

            if (token->sv.count + token->int_lit.suffixes.count > 2) {
                Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
                sp_die(1, SCHWASM_FILE_FMT " Register does not support loading %d-bit integers\n", schwasm_file_arg(schwasm->filename, token_line), (int) powl(2, token->sv.count + token->int_lit.suffixes.count));
            }

            schwasm_create_node(schwasm, (uint8_t) value);
            break;
        case SCHWASM_VALUE_DECIMAL:
            // TODO: check if out of range
            schwasm_create_node(schwasm, (uint8_t) schwasm_get_token(schwasm)->int_lit.value);
            break;
    }
}

void ld_reg(struct Schwasm *schwasm) {
    (void) schwasm;
}

void ld(struct Schwasm *schwasm, enum GCPU_REG reg) {
    schwasm_expect_org(schwasm);

    const Sp_Lexer_Token *token = schwasm_next_token(schwasm);

    if (!token) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
        sp_die(1, SCHWASM_FILE_FMT " Expected rhs\n", schwasm_file_arg(schwasm->filename, token_line));
    }

    if (token->type == TOK_Pound) {
        ld_imm(schwasm, reg);
    } else {
        ld_reg(schwasm);
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
                        sp_die(1, SCHWASM_FILE_FMT " ROM address does not support %d-bit integers\n", schwasm_file_arg(schwasm->filename, token_line), (int) powl(2, schwasm_get_token(schwasm)->sv.count + schwasm_get_token(schwasm)->int_lit.suffixes.count));
                    }

                    value = parse_hex_or_die(schwasm, schwasm_get_token(schwasm));

                    schwasm_create_node(schwasm, (uint8_t) value);
                    break;
                case SCHWASM_VALUE_DECIMAL:
                    // TODO: check if out of range
                    schwasm_create_node(schwasm, (uint8_t) schwasm_get_token(schwasm)->int_lit.value);
                    break;
            }

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

                    // TODO: turn this into a span definition instead of spamming memory segment definitions
                    for (int i = 0; i < value; ++i) {
                        schwasm_create_node(schwasm, 0x00);
                    }
                    break;
                case SCHWASM_VALUE_DECIMAL:
                    // TODO: check if out of range
                    value = (int) schwasm_get_token(schwasm)->int_lit.value;

                    // TODO: turn this into a span definition instead of spamming memory segment definitions
                    for (int i = 0; i < value; ++i) {
                        schwasm_create_node(schwasm, 0x00);
                    }
                    break;
            }
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

    while (schwasm.lexer.state != SPLEXER_TERMINATE) {
        splexer_tokenize(&schwasm.lexer);
    }

    sp_sb_appendf(&generated, "-- \tGenerated by Schwasm.\t --\n");
    sp_sb_appendf(&generated, "DEPTH = %d;\n", ROM_SIZE);
    sp_sb_appendf(&generated, "WIDTH = 8;\n\n");

    sp_sb_appendf(&generated, "ADDRESS_RADIX = HEX;\n");
    sp_sb_appendf(&generated, "DATA_RADIX = HEX;\n\n");

    sp_sb_appendf(&generated, "CONTENT\nBEGIN\n");

    const Sp_Lexer_Token *prev = NULL;
    const Sp_Lexer_Token *token = schwasm_get_token(&schwasm);
    Sp_Lexer_Token_Line prev_line = {0};
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm.lexer, token);

    if (!token) {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected initial token\n", schwasm_file_arg(schwasm.filename, token_line));
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
            schwasm_create_node(&schwasm, 0x00);
        } else if (sp_sv_eq(&sp_cstr_slice("TBA"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x01);
        } else if (sp_sv_eq(&sp_cstr_slice("LDAA"), &token->sv)) {
            ld(&schwasm, GCPU_REGA);
        } else if (sp_sv_eq(&sp_cstr_slice("LDAB"), &token->sv)) {
            ld(&schwasm, GCPU_REGB);
        } else if (sp_sv_eq(&sp_cstr_slice("SUM_BA"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x14);
        } else if (sp_sv_eq(&sp_cstr_slice("SUM_AB"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x15);
        } else if (sp_sv_eq(&sp_cstr_slice("AND_BA"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x16);
        } else if (sp_sv_eq(&sp_cstr_slice("AND_AB"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x17);
        } else if (sp_sv_eq(&sp_cstr_slice("OR_BA"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x18);
        } else if (sp_sv_eq(&sp_cstr_slice("OR_AB"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x19);
        } else if (sp_sv_eq(&sp_cstr_slice("COMA"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x1A);
        } else if (sp_sv_eq(&sp_cstr_slice("COMB"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x1B);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFA_L"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x1C);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFA_R"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x1D);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFB_L"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x1E);
        } else if (sp_sv_eq(&sp_cstr_slice("SHFB_R"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x1F);
        } else if (sp_sv_eq(&sp_cstr_slice("INX"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x30);
        } else if (sp_sv_eq(&sp_cstr_slice("INY"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x31);
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
            sp_sb_appendf(&generated, "[%04X..%04X]\t:\t%02X;\n", next_addr, node.addr - 1, 0x00);
        }
        next_addr = node.addr + 1;

        sp_sb_appendf(&generated, "%04X\t\t:\t%02X;\n", node.addr, node.code);

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
