// Test: Static const members visible in static_assert within the same struct
// This pattern is used in <ratio> header

struct TestStruct {
    static const int value = 42;
    static_assert(value == 42, "value should be 42");
};

template<int N>
struct TemplateStruct {
    static constexpr int computed = N * 2;
    static_assert(computed > 0, "computed should be positive");
};

int main() {
    // Force template instantiation
    TemplateStruct<5> ts;
    (void)ts;  // Suppress unused warning
    return TestStruct::value - 42;
}
