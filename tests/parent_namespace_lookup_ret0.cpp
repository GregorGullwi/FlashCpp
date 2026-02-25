// Test: parent namespace fallback in template lookup
// When inside std::__cxx11, looking up __detail::func should resolve std::__detail::func

namespace outer {
    namespace detail {
        template<typename T>
        T identity(T val) { return val; }
        
        int helper(int x) { return x + 1; }
    }
    
    namespace inner {
        // From within outer::inner, calling detail::identity should find outer::detail::identity
        int test_func(int x) {
            return detail::identity(x);
        }
        
        int test_non_template(int x) {
            return detail::helper(x);
        }
    }
}

int main() {
    int a = outer::inner::test_func(42);
    int b = outer::inner::test_non_template(99);
    return (a == 42 && b == 100) ? 0 : 1;
}
