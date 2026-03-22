// Phase 5: Tests that structs WITHOUT conversion operators compile and run correctly.
// Verifies that the Phase 5 existence-check guard does not break normal struct usage
// (no spurious conversions attempted). Expected return value: 0

struct PlainStruct {
    int x;
    int y;
    PlainStruct(int a, int b) : x(a), y(b) {}
    int sum() const { return x + y; }
};

PlainStruct make_pair(PlainStruct s) {
    return s;  // struct passed by value and returned by value, no conversion
}

int main() {
    PlainStruct s(10, 20);
    PlainStruct result = make_pair(s);
    if (result.sum() != 30) return 1;
    return 0;
}
