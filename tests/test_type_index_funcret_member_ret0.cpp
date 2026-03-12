// Regression test for: type_index stored only in encoded_metadata for struct-returning
// function calls (generateFunctionCallIr, generateMemberFunctionCallIr).
//
// The bug: makeExprResult(..., 0, 0, type_index_ull) sets ExprResult.type_index = 0
// and puts the real value only in encoded_metadata.  extractBaseFromOperands reads
// .type_index directly; when it is 0 it falls back to a linear scan of gTypeInfo
// looking for type_index_ == 0.  With two structs registered, type_index_ == 0 belongs
// to the FIRST struct (Decoy), not the one actually returned (Target), so the member
// lookup resolves against the wrong struct and either crashes (InternalError: member
// not found) or returns a value from the wrong field.
//
// Decoy is declared first so it receives type_index_ == 0 — the value the broken
// code always passes to extractBaseFromOperands for any struct-returning call.
// Target is declared second so it receives type_index_ > 0, which the bug discards.

struct Decoy {
    int dummy;
};

struct Target {
    int value;
};

// Path 1: free function returning Target
Target make_target() {
    Target t;
    t.value = 42;
    return t;
}

// Path 2: member function returning Target
struct Factory {
    Target produce() {
        Target t;
        t.value = 42;
        return t;
    }
};

int main() {
    // Without the fix, extractBaseFromOperands receives type_index=0, resolves to
    // Decoy (which has no "value" member), and the compiler throws InternalError or
    // silently reads Decoy::dummy (0) instead of Target::value (42).
    int a = make_target().value;    // must be 42

    Factory f;
    int b = f.produce().value;      // must be 42

    if (a != 42) return 1;
    if (b != 42) return 2;
    return 0;
}
