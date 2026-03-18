// Test: struct with operator bool() can be used where bool is expected
// Validates that buildConversionPlan does NOT return no_match for Struct→Bool
// (regression test for Phase 11 early-return in to==Bool block).
// C++20 [conv.bool]: user-defined conversion operators are valid for contextual bool.

struct Flag {
    int value;
    operator bool() const { return value != 0; }
};

// Function with a bool parameter - requires Struct→Bool via operator bool()
int process_bool(bool b) { return b ? 1 : 0; }

int test_struct_bool_init() {
    Flag truthy{5};
    Flag falsy{0};
    bool b1 = truthy;  // operator bool()
    bool b2 = falsy;   // operator bool()
    return (b1 ? 0 : 1) + (b2 ? 1 : 0);  // expect 0
}

int test_struct_bool_function_arg() {
    Flag truthy{7};
    Flag falsy{0};
    int r1 = process_bool(truthy);  // expect 1
    int r2 = process_bool(falsy);   // expect 0
    return (r1 - 1) + r2;           // expect 0
}

int test_struct_bool_condition() {
    Flag on{1};
    Flag off{0};
    int result = 0;
    if (on) result += 1;   // operator bool() returns true
    if (!off) result += 1; // operator bool() returns false, negated → true
    return result - 2;     // expect 0
}

int main() {
    return test_struct_bool_init()
         + test_struct_bool_function_arg()
         + test_struct_bool_condition();
}
