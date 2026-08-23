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

typedef Sp_Dynamic_Array(struct Schwasm_Node) Schwasm_Nodes;

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

enum Schwasm_Value_Type {
    SCHWASM_VALUE_HEX,
    SCHWASM_VALUE_DECIMAL,
};

SPExtern struct Schwasm schwasm_init(const char *filename);
SPExtern void schwasm_expect_org(struct Schwasm *schwasm);
SPExtern void schwasm_create_node(struct Schwasm *schwasm, enum Schwasm_Op op, uint16_t dword);
SPExtern const Sp_Lexer_Token *schwasm_get_token(struct Schwasm *schwasm);
SPExtern const Sp_Lexer_Token *schwasm_peek_token(struct Schwasm *schwasm);
SPExtern const Sp_Lexer_Token *schwasm_next_token(struct Schwasm *schwasm);
SPExtern enum Schwasm_Value_Type schwasm_expect_value(struct Schwasm *schwasm);

SPExtern Schwasm_Nodes schwasm_generate_ir(struct Schwasm *schwasm);

SPExtern void schwasm_destroy(struct Schwasm *schwasm);
