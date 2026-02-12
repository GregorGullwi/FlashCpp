// Test member template with truncated body
struct Outer {
    template <typename T>
    struct Inner {
        T value;
        // Missing closing braces for both Inner and Outer
