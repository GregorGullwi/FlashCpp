// Test: String literal concatenation in static_assert message
// This pattern is used in <ratio> header for long error messages

struct Test {
    static_assert(true, 
        "This is a multi-line string literal message "
        "that spans across multiple lines "
        "using C++ string concatenation.");
};

int main() {
    return 0;
}
