// Regression test for: type_index stored only in encoded_metadata for struct-returning
// function calls (generateFunctionCallIr, generateMemberFunctionCallIr).
//
// The bug: makeExprResult(..., 0, 0, type_index_ull) sets ExprResult.type_index = 0
// and puts the real value only in encoded_metadata.  extractBaseFromOperands reads
// .type_index directly and passes it to gLazyMemberResolver.  With type_index=0 the
// resolver looks up gTypeInfo[0], which is a native type (not a struct), so the
// direct-index lookup fails.  The fallback linear scan then searches for a TypeInfo
// with type_index_ == 0 — user structs start at index > 0, so the scan also finds
// nothing, and the code throws InternalError("struct type info not found").
//
// Currently the crash is prevented by a pair of redundant toExprResult() wrappers in
// generateMemberAccessIr (lines 1003 and 1116 of CodeGen_MemberAccess.cpp) that
// encode/decode the ExprResult through ExprOperands, recovering the correct type_index
// from encoded_metadata.  This test will fail if those wrappers are removed without
// first fixing makeExprResult to populate type_index correctly, and will also catch
// any future regression where type_index is again lost at the producer site.
//
// Two structs are declared so the fallback scan cannot accidentally succeed by finding
// a struct with type_index_ == 0 (user structs always get index > 0 in gTypeInfo).

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
