#include <errno.h>
#include <math.h>
#include <splexer.h>
#include <sptl.h>

#define TODO_LINE 1337

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
};

static inline struct Schwasm schwasm_init() {
    struct Schwasm schwasm = {
        .nodes = {
            .cmp = &schwasm_node_lesser_cmp,
        },
    };

    return schwasm;
}

static inline void schwasm_create_node(struct Schwasm *schwasm, uint8_t code) {
    if (sp_bitset_check(&schwasm->used_addrs, schwasm->addr)) {
        sp_die(1, "Line %d: Address collision", TODO_LINE);
    }
    sp_heap_push(&schwasm->nodes, ((struct Schwasm_Node) {.addr = schwasm->addr, .code = code}));
    sp_bitset_set(&schwasm->used_addrs, schwasm->addr);
    ++schwasm->addr;
}

static inline const Sp_Lexer_Token *schwasm_get_token(struct Schwasm *schwasm) {
    if (schwasm->lexer_attr.idx >= schwasm->lexer.tokens.count) {
        return NULL;
    }

    sp_log(SP_INFO, SP_SV_FMT, sp_sv_arg(schwasm->lexer.tokens.data[schwasm->lexer_attr.idx].sv));

    return &schwasm->lexer.tokens.data[schwasm->lexer_attr.idx];
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

    if (!token ||
        splexer_token_get_line(&schwasm->lexer, prev).line != splexer_token_get_line(&schwasm->lexer, token).line) {
        sp_die(1, "Line %d: Expected value", TODO_LINE);
    }

    if (sp_sv_eq(&sp_cstr_slice("$"), &token->sv)) {
        prev = schwasm_get_token(schwasm);
        token = schwasm_next_token(schwasm);

        if (!token) {
            sp_die(1, "Line %d: Unexpected EOF", TODO_LINE);
        }

        if (splexer_token_get_line(&schwasm->lexer, prev).line != splexer_token_get_line(&schwasm->lexer, token).line) {
            sp_die(1, "Line %d: Expected hexadecimal value after $", TODO_LINE);
        }

        if (token->type != TOK_IntLiteral && token->type != TOK_ID) {
            sp_die(SP_ERROR, "Line %d: Unexpected token after $", TODO_LINE);
        }

        return SCHWASM_VALUE_HEX;
    } else if (token->type == TOK_IntLiteral) {
        return SCHWASM_VALUE_DECIMAL;
    } else {
        sp_die(1, "Line %d: Unexpected value", TODO_LINE);
        return 1;
    }
}

static inline void schwasm_destroy(struct Schwasm *schwasm) {
    splexer_destroy(&schwasm->lexer);
    sp_bitset_free(&schwasm->used_addrs);
    sp_heap_free(&schwasm->nodes);
}

enum GCPU_REG {
    GCPU_REGA,
    GCPU_REGB,
    GCPU_REGX,
    GCPU_REGY,
};

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

    switch (schwasm_expect_value(schwasm)) {
        case SCHWASM_VALUE_HEX:
            if (schwasm_get_token(schwasm)->sv.count > 2) {
                sp_die(1, "Line %d: Register does not support loading %d-bit integers", TODO_LINE, (int) powl(2, schwasm_get_token(schwasm)->sv.count));
            }

            errno = 0;
            int value = (int) strtol(schwasm_get_token(schwasm)->sv.ptr, NULL, 16);

            if (errno != 0) {
                sp_die(1, "Line %d: Error parsing hexadecimal value", TODO_LINE);
            }

            schwasm_create_node(schwasm, (uint8_t) value);
            break;
        case SCHWASM_VALUE_DECIMAL:
            schwasm_create_node(schwasm, (uint8_t) schwasm_get_token(schwasm)->int_lit.value);
            break;
    }
}

void ld_reg(struct Schwasm *schwasm) {
    (void) schwasm;
}

void ld(struct Schwasm *schwasm, enum GCPU_REG reg) {
    const Sp_Lexer_Token *token = schwasm_next_token(schwasm);

    if (!token) {
        sp_die(1, "Line %d: Unexpected rhs", TODO_LINE);
    }

    if (token->type == TOK_Pound) {
        ld_imm(schwasm, reg);
    } else {
        ld_reg(schwasm);
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
        sp_die(1, "Not enough arguments (expected a filename)");
    }
    if (argc > 2) {
        sp_die(1, "Too many arguments (expected 1, got %d)", argc - 1);
    }

    schwasm = schwasm_init();
    atexit(cleanup);

    if (splexer_init(&schwasm.lexer, argv[1]) != 0) {
        sp_die(1, "Unable to open file: \"%s\"", argv[1]);
    }

    while (schwasm.lexer.state != SPLEXER_TERMINATE) {
        splexer_tokenize(&schwasm.lexer);
    }

    sp_sb_appendf(&generated, "-- \tGenerated by Schwasm.\t --\n");
    sp_sb_appendf(&generated, "DEPTH = 4096;\n");
    sp_sb_appendf(&generated, "WIDTH = 8;\n\n");

    sp_sb_appendf(&generated, "ADDRESS_RADIX = HEX;\n");
    sp_sb_appendf(&generated, "DATA_RADIX = HEX;\n\n");

    sp_sb_appendf(&generated, "CONTENT\nBEGIN\n");

    const Sp_Lexer_Token *prev = NULL;
    const Sp_Lexer_Token *token = schwasm_get_token(&schwasm);

    if (!token) {
        sp_die(1, "Line %d: Unexpected initial token", TODO_LINE);
    }

    do {
        if (prev && splexer_token_get_line(&schwasm.lexer, prev).line != splexer_token_get_line(&schwasm.lexer, token).line) {
            schwasm.lexer_attr.busy = false;
        } else if (schwasm.lexer_attr.busy) { // some token after a completed instruction
            sp_die(1, "Line %d: Trailing tokens after instruction", TODO_LINE);
        }

        if (token->type != TOK_ID) {
            sp_die(1,
                   "Line %d: Failed to parse unknown symbol \"%s\"",
                   TODO_LINE,
                   SPLEXER_TOKENS_LITERAL[token->type]);
        }

        schwasm.lexer_attr.busy = true;

        if (sp_sv_eq(&sp_cstr_slice("ORG"), &token->sv)) {
        } else if (sp_sv_eq(&sp_cstr_slice("TAB"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x00);
        } else if (sp_sv_eq(&sp_cstr_slice("TBA"), &token->sv)) {
            schwasm_create_node(&schwasm, 0x01);
        } else if (sp_sv_eq(&sp_cstr_slice("LDAA"), &token->sv)) {
            ld(&schwasm, GCPU_REGA);
        } else if (sp_sv_eq(&sp_cstr_slice("LDAB"), &token->sv)) {
            ld(&schwasm, GCPU_REGB);
        } else {
            sp_die(1, "Line %d: Failed to parse unknown instruction \"" SP_SV_FMT "\"", TODO_LINE, sp_sv_arg(token->sv));
        }

        prev = schwasm_get_token(&schwasm);
    } while ((token = schwasm_next_token(&schwasm)));

    while (schwasm.nodes.count > 0) {
        struct Schwasm_Node node = sp_heap_top(&schwasm.nodes);

        sp_sb_appendf(&generated, "%04X : %02X;\n", node.addr, node.code);

        sp_heap_pop(&schwasm.nodes);
    }

    sp_sb_appendf(&generated, "END;\n");
    printf("%s", generated.data);
    return 0;
}
