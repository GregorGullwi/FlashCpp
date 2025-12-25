// Test: C++20 Template Argument Disambiguation - Basic Cases
// Tests that '<' after qualified identifiers is correctly parsed as template arguments
// rather than as a less-than comparison operator

namespace ns {
    template<typename T>
    int func() {
        return 42;
    }
}

// Test 1: Basic qualified identifier with template arguments
int test1() {
    return ns::func<int>();
}

// Test 2: Qualified identifier with template in expression context
int test2() {
    int x = ns::func<int>();
    return x;
}

int main() {
    return test1();
}
