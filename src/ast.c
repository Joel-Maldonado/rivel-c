#include "ast.h"

const char *type_display_name(Type type) {
    switch (type.kind) {
        case TYPE_INT:
            return "Int";
        case TYPE_BOOL:
            return "Bool";
    }

    return "<type>";
}

bool type_equal(Type lhs, Type rhs) {
    return lhs.kind == rhs.kind;
}
