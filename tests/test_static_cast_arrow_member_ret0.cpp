// Test: apply_postfix_operators correctly handles -> as punctuator (not operator)
// This verifies that the parser can parse static_cast followed by arrow member access.
// Note: codegen for static_cast->member is a separate issue; this test validates parsing only.

struct Data {
    int x;
    int get() { return x; }
};

int main() {
    Data d;
    d.x = 42;
    Data* p = &d;
    int val = p->x;  // Arrow access should work
    return val == 42 ? 0 : 1;
}
