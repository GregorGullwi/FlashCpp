// Phase 2: Comprehensive unified parser test
// Tests various qualified identifier patterns with template arguments

namespace test {
    namespace nested {
        namespace deep {
            template<typename T>
            T identity(T x) {
                return x;
            }
            
            template<typename A, typename B>
            int sum(A a, B b) {
                return static_cast<int>(a) + static_cast<int>(b);
            }
        }
    }
}

// Test 1: Deep nesting with single template parameter
int test1() {
    return test::nested::deep::identity<int>(5);
}

// Test 2: Deep nesting with multiple template parameters
int test2() {
    return test::nested::deep::sum<int, int>(10, 15);
}

// Test 3: Using the unified parser in decltype context
int test3() {
    using result_t = decltype(test::nested::deep::identity<int>(42));
    result_t x = 8;
    return x;
}

// Test 4: Qualified identifier in expression
int test4() {
    return test::nested::deep::identity<int>(3) + test::nested::deep::identity<int>(4);
}

int main() {
    return test1() + test2() + test3() + test4();  // 5 + 25 + 8 + 7 = 45
}
