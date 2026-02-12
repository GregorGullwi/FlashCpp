// Test that truncated template class body doesn't cause infinite loop
// This test file is intentionally malformed - missing closing brace
template <typename T>
struct Foo {
    T value;
    // Missing closing brace - should error, not hang
