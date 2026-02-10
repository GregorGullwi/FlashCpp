// Test: function template with explicit template args as expression statement
// Verifies that func<Args>(...) is parsed as function call, not declaration
// This pattern appears in variant:499 for std::__do_visit<void>(...)

template<typename T>
void do_visit(T val) {
}

void test() {
    do_visit<int>(42);
}

int main() {
    test();
    return 0;
}
