#include "schwasm.h"
#include "ir.h"

#include <assert.h>
#include <sptl.h>

struct Schwasm schwasm_init(const char *filename) {
    struct Schwasm schwasm = {
        .filename = filename,
    };

    return schwasm;
}

void schwasm_expect_org(struct Schwasm *schwasm) {
    if (!schwasm->addr_valid) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " No preceding ORG to define initial address\n", schwasm_file_arg(schwasm->filename, token_line));
    } else if (schwasm->addr > ADDR_END) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " Address out of bounds\n", schwasm_file_arg(schwasm->filename, token_line));
    }
}

void schwasm_create_node(struct Schwasm *schwasm, enum Schwasm_Op op, uint16_t dword) {
    schwasm_expect_org(schwasm);

    struct Schwasm_Node node = (struct Schwasm_Node) {
        .op = op,
        .addr = schwasm->addr,
    };

    // Address collision check
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
    uint16_t count = (op == SCHWASM_OP_UNKNOWN) ? dword : SCHWASM_OP_COUNT[op]; // if SCHWASM_OP_UNKNOWN we are doing DS
    for (uint16_t i = 0; i < count; ++i) {
        if (schwasm->addr > ADDR_END) {
            sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%04X)\n", schwasm_file_arg(schwasm->filename, token_line), schwasm->addr);
        }
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

    sp_da_push(&schwasm->nodes, node);
}

const Sp_Lexer_Token *schwasm_get_token(struct Schwasm *schwasm) {
    if (schwasm->lexer_attr.idx >= schwasm->lexer.tokens.count) {
        return NULL;
    }
    return &schwasm->lexer.tokens.data[schwasm->lexer_attr.idx];
}

const Sp_Lexer_Token *schwasm_peek_token(struct Schwasm *schwasm) {
    if (schwasm->lexer_attr.idx + 1 >= schwasm->lexer.tokens.count) {
        return NULL;
    }

    return &schwasm->lexer.tokens.data[schwasm->lexer_attr.idx + 1];
}

const Sp_Lexer_Token *schwasm_next_token(struct Schwasm *schwasm) {
    ++schwasm->lexer_attr.idx;
    return schwasm_get_token(schwasm);
}

enum Schwasm_Value_Type schwasm_expect_value(struct Schwasm *schwasm) {
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

void schwasm_destroy(struct Schwasm *schwasm) {
    splexer_destroy(&schwasm->lexer);
    sp_bitset_free(&schwasm->used_addrs);
    sp_da_free(&schwasm->nodes);
    sp_ht_free(&schwasm->equ_table);
}

static inline void schwasm_generate_ir__loop(struct Schwasm *schwasm);

void schwasm_generate_ir(struct Schwasm *schwasm) {
    Sp_Lexer_Return_Code code;
    do {
    } while ((code = splexer_tokenize(&schwasm->lexer)) == SPLEXER_OK);

    if (code == SPLEXER_ERROR) {
        if (schwasm->lexer.state == SPLEXER_STATE_MULTICOMMENT) {
            sp_die(1, "C-style multi-line comments are not supported!\n");
        } else {
            sp_die(1, "Unknown tokenizer error occurred!\n");
        }
    }

    schwasm_generate_ir__loop(schwasm);
}

static inline void schwasm_generate_ir__loop(struct Schwasm *schwasm) {
    const Sp_Lexer_Token *prev = NULL;
    const Sp_Lexer_Token *token = schwasm_get_token(schwasm);
    Sp_Lexer_Token_Line prev_line = {0};
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, token);
    void *data = NULL;

    if (!token) {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected initial token\n", schwasm_file_arg(schwasm->filename, ((Sp_Lexer_Token_Line) {
                                                                                                          .line = 1,
                                                                                                          .col = 1,
                                                                                                      })));
    }

    do {
        // prev_line is calculated at the end of previous iteration
        token_line = splexer_token_get_line(&schwasm->lexer, token);
        if (prev && prev_line.line != token_line.line) {
            schwasm->lexer_attr.busy = false;
        } else if (schwasm->lexer_attr.busy) { // some token after a completed instruction
            sp_die(1, SCHWASM_FILE_FMT " Trailing tokens after instruction\n", schwasm_file_arg(schwasm->filename, token_line));
        }

        if (token->type != TOK_ID) {
            sp_die(1,
                   SCHWASM_FILE_FMT " Failed to parse unknown symbol \"%s\"\n",
                   schwasm_file_arg(schwasm->filename, token_line),
                   SPLEXER_TOKENS_LITERAL[token->type]);
        }

        schwasm->lexer_attr.busy = true;

        Schwasm_Dispatcher *dispatcher = schwasm_dispatcher_get_sv(token->sv);
        if (!dispatcher) {
            sp_die(1, SCHWASM_FILE_FMT " Failed to parse unknown instruction \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, token_line), sp_sv_arg(token->sv));
        }

        dispatcher(schwasm, data);

        prev = schwasm_get_token(schwasm);
        prev_line = splexer_token_get_line(&schwasm->lexer, prev);
    } while ((token = schwasm_next_token(schwasm)));
}
