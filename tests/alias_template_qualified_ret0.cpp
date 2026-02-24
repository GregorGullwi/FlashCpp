// Test: alias template with namespace-qualified target type
// Verifies that 'using alias = ns::type<T>' works correctly

namespace ns1 {
    template<typename T>
    struct vec {
        T val;
        T get() const { return val; }
    };
}

template<typename T>
using my_vec = ns1::vec<T>;

int main() {
    my_vec<int> v;
    v.val = 42;
    return v.val == 42 ? 0 : 1;
}
