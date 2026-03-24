// Tests that converting constructors with const-reference parameters correctly
// perform the pre-bind value conversion (e.g. int->double) before binding.
struct IntTarget {
    int value;
    IntTarget(const double& d) : value(static_cast<int>(d)) {}
};

struct LongTarget {
    long long value;
    LongTarget(const double& d) : value(static_cast<long long>(d)) {}
};

struct FloatTarget {
    float value;
    FloatTarget(const float& f) : value(f) {}
};

int takeIntTargetValue(IntTarget target) {
    return target.value;
}

struct TargetReader {
    int takeLong(LongTarget target) const { return static_cast<int>(target.value); }
    int takeFloat(FloatTarget target) const { return static_cast<int>(target.value); }
};

IntTarget makeIntTargetFromLiteral() {
    return 42;
}

LongTarget makeLongTargetFromExpr() {
    return 40 + 2;
}

FloatTarget makeFloatTargetFromLiteral() {
    return 7;
}

int main() {
    // Copy-init: sema selects IntTarget(const double&) for int source
    IntTarget t1 = 42;
    if (t1.value != 42) return 1;

    IntTarget t2 = 100;
    if (t2.value != 100) return 2;

    // Test with different integral type source
    LongTarget l1 = 99;
    if (l1.value != 99) return 3;

    // Non-identifier expression source should still materialize a reference temporary.
    IntTarget t3 = 40 + 2;
    if (t3.value != 42) return 4;

    // Direct-call argument conversion/materialization.
    if (takeIntTargetValue(42) != 42) return 5;
    if (takeIntTargetValue(40 + 2) != 42) return 6;

    // Member-call argument conversion/materialization for different destination types.
    TargetReader reader;
    if (reader.takeLong(42) != 42) return 7;
    if (reader.takeLong(40 + 2) != 42) return 8;
    if (reader.takeFloat(7) != 7) return 9;

    // Return conversion/materialization from both literals and expressions.
    IntTarget from_literal = makeIntTargetFromLiteral();
    if (from_literal.value != 42) return 10;

    LongTarget from_expr = makeLongTargetFromExpr();
    if (from_expr.value != 42) return 11;

    FloatTarget from_float_literal = makeFloatTargetFromLiteral();
    if (static_cast<int>(from_float_literal.value) != 7) return 12;

    return 0;
}
