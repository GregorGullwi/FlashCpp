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

int main() {
    // Copy-init: sema selects IntTarget(const double&) for int source
    IntTarget t1 = 42;
    if (t1.value != 42) return 1;

    IntTarget t2 = 100;
    if (t2.value != 100) return 2;

    // Test with different integral type source
    LongTarget l1 = 99;
    if (l1.value != 99) return 3;

    return 0;
}
