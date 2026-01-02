// Test to verify deprecation warning when using standard library
// This should trigger the warning if the standard library uses __is_literal_type

// Don't include <type_traits> as it causes parsing issues
// Instead, test the builtin directly which is what the library would use

int main() {
    // Simulating what std::is_literal_type would do internally
    bool result = __is_literal_type(int);
    return result ? 0 : 1;
}
