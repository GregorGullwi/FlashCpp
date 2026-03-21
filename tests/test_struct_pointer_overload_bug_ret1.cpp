// Test: overload resolution for struct pointer parameters.
//
// Two overloads:  process(Foo*)  and  process(Bar*)
// Calling process(&bar) should unambiguously select process(Bar*) and return 1.
//
// Regression test for: buildConversionPlan's pointer-to-pointer path must
// compare type_index for struct pointers, not just the resolved Type enum.
// Without the type_index check, Foo* and Bar* both resolve to Type::Struct
// and score ExactMatch, causing spurious ambiguity.

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
