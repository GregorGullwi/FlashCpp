// Phase 3 test: Simple decltype with template argument disambiguation
// Tests that < after qualified-id in decltype is correctly parsed as template arguments

namespace ns {
    template<typename T>
    T add(T a, T b) {
        return a + b;
    }
}

int main() {
    // Phase 3: Decltype context - < after ns::add should be template arguments, not less-than
    using result_t = decltype(ns::add<int>(1, 2));
    
    // Verify the type works
    result_t x = 10;
    result_t y = 25;
    
    return ns::add<int>(x, y);  // 10 + 25 = 35
}
