// Test: overload resolution for struct pointer parameters.
//
// Two overloads:  process(Foo*)  and  process(Bar*)
// Calling process(&bar) should unambiguously select process(Bar*) and return 1.
//
// BUG (pre-existing): buildConversionPlan's pointer-to-pointer path compares
// only the resolved Type enum (both are Type::Struct) without checking
// type_index, so Foo* and Bar* both score ExactMatch.  This causes overload
// resolution to report ambiguity or select the wrong candidate.
//
// When the bug is fixed, this test should return 1.

struct Foo {
    int x;
};

struct Bar {
    int y;
};

int process(Foo* f) {
    return 0;
}

int process(Bar* b) {
    return 1;
}

int main() {
    Bar bar;
    bar.y = 42;
    return process(&bar);  // Should select process(Bar*) → returns 1
}
