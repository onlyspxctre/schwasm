#include "schwasm.h"
typedef void(Schwasm_Dispatcher)(struct Schwasm *);

extern Schwasm_Dispatcher *schwasm_dispatcher_get(const char *str);
extern Schwasm_Dispatcher *schwasm_dispatcher_get_sv(Sp_String_View sv);
