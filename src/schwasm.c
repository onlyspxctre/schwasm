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
    } else if (schwasm->addr > RAM_END) {
        Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
        sp_die(1, SCHWASM_FILE_FMT " Address out of bounds\n", schwasm_file_arg(schwasm->filename, token_line));
    }
}

struct Schwasm_Node *schwasm_create_node(struct Schwasm *schwasm, enum Schwasm_Op op, uint16_t dword) {
    schwasm_expect_org(schwasm);

    struct Schwasm_Node node = (struct Schwasm_Node) {
        .op = op,
        .addr = schwasm->addr,
    };

    // Address collision check
    Sp_Lexer_Token_Line token_line = splexer_token_get_line(&schwasm->lexer, schwasm_get_token(schwasm));
    uint16_t count = (op == SCHWASM_OP_UNKNOWN) ? dword : SCHWASM_OP_COUNT[op]; // if SCHWASM_OP_UNKNOWN we are doing DS
    for (uint16_t i = 0; i < count; ++i) {
        if (schwasm->addr > ROM_END && node.addr <= ROM_END) {
            sp_die(1, SCHWASM_FILE_FMT " Address crosses ROM/RAM boundary\n", schwasm_file_arg(schwasm->filename, token_line));
        }
        if (schwasm->addr > RAM_END) {
            sp_die(1, SCHWASM_FILE_FMT " Address out of bounds (0x%04X)\n", schwasm_file_arg(schwasm->filename, token_line), schwasm->addr);
        }
        if (sp_bitset_check(&schwasm->used_addrs, schwasm->addr)) {
            // TODO: make this error msg elaborate on what address it is colliding with
            sp_die(1, SCHWASM_FILE_FMT " Address collision\n", schwasm_file_arg(schwasm->filename, token_line));
        }

        sp_bitset_set(&schwasm->used_addrs, schwasm->addr);
        ++schwasm->addr;
    }

    schwasm_node_edit(&node, dword);
    sp_da_push(&schwasm->nodes, node);

    return &schwasm->nodes.data[schwasm->nodes.count - 1];
}

void* schwasm_depend_label(struct Schwasm *schwasm, Sp_String_View label) {
    sp_ht_node_t(&schwasm->label_table)* query = NULL;

    sp_ht_get(&schwasm->label_table, label, &query);

    if (query) return query;
    sp_ht_insert(&schwasm->label_table, label, ((struct Schwasm_Label_Entry) { .defined = false }));
    sp_ht_get(&schwasm->label_table, label, &query);

    return query;
}

void* schwasm_define_label(struct Schwasm *schwasm, const Sp_String_View *label, uint16_t value) {
    sp_ht_node_t(&schwasm->label_table) *query = NULL;
    sp_ht_get(&schwasm->label_table, *label, &query);

    if (query) {
        if (query->value.defined) {
            return NULL;
        }
        for (size_t i = 0; i < query->value.deferred.count; ++i) {
            schwasm_node_edit(&schwasm->nodes.data[query->value.deferred.data[i].index], value);
        }
        sp_da_free(&query->value.deferred);
        query->value.deferred.count = 0;
        query->value.deferred.capacity = 0;
        query->value.defined = true;
        query->value.value = value;
    } else {
        sp_ht_insert(&schwasm->label_table, *label, ((struct Schwasm_Label_Entry) {.defined = true, .value = value}));
        sp_ht_get(&schwasm->label_table, *label, &query);
    }

    return query;
}

inline void schwasm_node_edit(struct Schwasm_Node *node, uint16_t dword) {
    switch (SCHWASM_OP_COUNT[node->op]) {
        case 0:
            node->dword = dword;
            break;
        case 1:
        case 2:
            node->word = (uint8_t) dword;
            break;
        case 3:
            node->lword = (uint8_t) dword;
            node->hword = (uint8_t) (dword >> 8);
            break;
        default:
            sp_unreachable();
    }
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
    } else if (token->type == TOK_ID) {
        return SCHWASM_VALUE_LABEL;
    } else {
        sp_die(1, SCHWASM_FILE_FMT " Unexpected value\n", schwasm_file_arg(schwasm->filename, prev_line));
        return 1;
    }
}

void schwasm_destroy(struct Schwasm *schwasm) {
    splexer_destroy(&schwasm->lexer);
    sp_da_free(&schwasm->nodes);
    sp_ht_free(&schwasm->label_table);
    sp_bitset_free(&schwasm->used_addrs);
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

    for (size_t i = 0; i < schwasm->label_table.capacity; ++i) {
        if (schwasm->label_table.psls[i] == SP_HT_PSL_SENTINEL) {
            continue;
        }

        if (!schwasm->label_table.buckets[i].value.defined) {
            sp_die(1, SCHWASM_FILE_FMT " Undefined label: \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, schwasm->label_table.buckets[i].value.deferred.data[0].token_line), sp_sv_arg(schwasm->label_table.buckets[i].key));
        }
    }
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
            const Sp_Lexer_Token *peek = schwasm_peek_token(schwasm);
            Sp_Lexer_Token_Line peek_line = splexer_token_get_line(&schwasm->lexer, peek);

            if (!peek || peek->type != TOK_ID || token_line.line != peek_line.line || !(dispatcher = schwasm_dispatcher_get_sv(peek->sv))) {
                sp_die(1, SCHWASM_FILE_FMT " Failed to parse unknown instruction \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, token_line), sp_sv_arg(token->sv));
            }

            if (sp_sv_eq(&peek->sv, &sp_cstr_slice("ORG"))) {
                sp_die(1, SCHWASM_FILE_FMT " Preceding label on ORG disallowed\n", schwasm_file_arg(schwasm->filename, token_line));
            }

            // TODO: dedicated function for label definitions
            if (!sp_sv_eq(&peek->sv, &sp_cstr_slice("EQU"))) {

                if (!schwasm_define_label(schwasm, &token->sv, schwasm->addr)) {
                    sp_die(1, SCHWASM_FILE_FMT " Cannot redefine label \"" SP_SV_FMT "\"\n", schwasm_file_arg(schwasm->filename, token_line), sp_sv_arg(token->sv));
                }
            } else {
                // The behavior of EQU is dispatched via Schwasm_Dispatcher. We send the `String_View` to the `EQU` dispatcher
                data = (void *) &token->sv;
            }

            schwasm_next_token(schwasm); // consume away the current token (label)
        }

        dispatcher(schwasm, data);

        prev = schwasm_get_token(schwasm);
        prev_line = splexer_token_get_line(&schwasm->lexer, prev);
    } while ((token = schwasm_next_token(schwasm)));
}
