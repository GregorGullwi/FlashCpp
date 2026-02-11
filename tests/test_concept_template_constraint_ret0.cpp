// Test: Concept used in template context should not cause "No primary template found" errors
// This tests that concepts are properly skipped in try_instantiate_class_template

template<typename T>
concept Addable = requires(T a, T b) { a + b; };

template<Addable T>
T add(T a, T b) {
    return a + b;
}

int main() {
    return add(3, 4) - 7;  // Should return 0
}
