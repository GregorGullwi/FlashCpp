// Regression test for: member-function Phase 2 fallback suppresses free-function discovery.
//
// Struct A has a member operator+(int), but the expression `a + b` uses struct B
// as the RHS. The correct overload is the free function operator+(const A&, const B&).
//
// BUG: findBinaryOperatorOverload's Phase 2 (OverloadResolution.h:723-725) returns
// A::operator+(int) as a fallback (has_match=true) even though `int` != `B`.
// findBinaryOperatorOverloadWithFreeFunction then short-circuits at line 755-757
// and never searches for the free function.
//
// Expected: free operator+(A, B) is called, returns A{30} → main returns 0.
// Actual (buggy): member A::operator+(int) is selected with wrong argument type.

struct B {
    int value;
    B(int v) : value(v) {}
};

struct A {
    int value;
    A(int v) : value(v) {}

    // Member operator+ that takes int (NOT B)
    A operator+(int rhs) {
        return A(value + rhs);
    }
};

// Free-function operator+ that takes (A, B) — this is the correct overload for `a + b`
A operator+(const A& a, const B& b) {
    return A(a.value + b.value);
}

int main() {
    A a(10);
    B b(20);
    A result = a + b;  // Should call free operator+(A, B), not member A::operator+(int)
    if (result.value == 30) {
        return 0;  // success: free function was called
    }
    return 1;  // failure: wrong overload selected
}
