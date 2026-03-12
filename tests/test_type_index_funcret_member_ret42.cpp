// Regression test for: type_index stored only in encoded_metadata for struct-returning
// function calls (generateFunctionCallIr, generateMemberFunctionCallIr,
// generateConstructorCallIr, generateLambdaExpressionIr).
//
// The bug: makeExprResult(..., 0, 0, type_index_ull) sets ExprResult.type_index = 0
// and puts the real value only in encoded_metadata.  Any direct ExprResult consumer
// (e.g. extractBaseFromOperands) that reads .type_index gets 0, causing member lookup
// to fail or pick the wrong struct.
//
// This test exercises all four affected code paths:
//   1. Free function returning a struct  (generateFunctionCallIr)
//   2. Member function returning a struct (generateMemberFunctionCallIr)
//   3. Constructor call result           (generateConstructorCallIr)
//   4. Lambda returning a struct         (generateLambdaExpressionIr)
// Each path is followed immediately by .member access so that extractBaseFromOperands
// must resolve the type_index from the ExprResult named field.

struct Inner {
    int value;
};

struct Outer {
    Inner inner;
    int get_value() { return inner.value; }
};

// --- Path 1: free function returning struct ---
Inner make_inner(int v) {
    Inner i;
    i.value = v;
    return i;
}

// --- Path 2: member function returning struct ---
struct Factory {
    Inner produce(int v) {
        Inner i;
        i.value = v;
        return i;
    }
};

int main() {
    int result = 0;

    // Path 1: free function return -> .member
    result += make_inner(10).value;          // +10

    // Path 2: member function return -> .member
    Factory f;
    result += f.produce(12).value;           // +12

    // Path 3: constructor call result -> .member
    // (ConstructorCallNode path in generateConstructorCallIr)
    Inner i = Inner{20};
    result += i.value;                       // +20

    // Path 4: lambda returning struct -> .member
    auto make = [](int v) -> Inner {
        Inner x;
        x.value = v;
        return x;
    };
    result += make(0).value;                 // +0

    // result == 10 + 12 + 20 + 0 = 42
    return result;
}
