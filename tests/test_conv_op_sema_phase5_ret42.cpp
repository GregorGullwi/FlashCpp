// Phase 5: Tests for conversion operator sema annotation accuracy.
// Verifies that struct→primitive conversion via conversion operator still works
// correctly after the Phase 5 existence-check guard in tryAnnotateConversion.
// Expected return value: 42

struct WithConvOp {
    int value;
    explicit WithConvOp(int v) : value(v) {}
    operator int() const { return value; }
    operator double() const { return static_cast<double>(value); }
};

struct InheritedConvOp : WithConvOp {
    explicit InheritedConvOp(int v) : WithConvOp(v) {}
};

int take_int(int x) { return x; }
double take_double(double x) { return x > 41.5 ? 42 : 0; }

int main() {
    // Basic conversion operator in function arg
    WithConvOp obj(42);
    int r1 = take_int(obj);           // struct→int via operator int()
    if (r1 != 42) return 1;

    // Conversion operator in variable init
    int r2 = obj;                      // struct→int via operator int()
    if (r2 != 42) return 2;

    // Conversion operator in return (function take_double)
    double r3 = take_double(obj);      // struct→double via operator double()
    if (static_cast<int>(r3) != 42) return 3;

    // Inherited conversion operator
    InheritedConvOp inherited(42);
    int r4 = take_int(inherited);      // inherited operator int()
    if (r4 != 42) return 4;

    return 42;
}
