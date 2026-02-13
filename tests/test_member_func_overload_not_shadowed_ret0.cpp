// Test: member function overload should not be shadowed by template function lookup
// When a class has a member function and a namespace template function has the same name,
// calling the member function from within the class should resolve to the member, not the template.
namespace ns_impl {
    template<bool B>
    bool compare_exchange_weak(int* ptr, int& expected, int desired, int success, int failure) {
        return B;
    }
}

template<typename T, bool = false>
struct MyAtomic;

template<typename T>
struct MyAtomic<T, false> {
    T* _M_ptr;
    
    bool compare_exchange_weak(T& expected, T desired, int success, int failure) const {
        return ns_impl::compare_exchange_weak<true>(_M_ptr, expected, desired, success, failure);
    }
    
    // This 2-arg overload calls the 4-arg member overload above.
    // It must NOT be hijacked by the namespace template ns_impl::compare_exchange_weak<>.
    bool compare_exchange_weak(T& expected, T desired, int order = 5) const {
        return compare_exchange_weak(expected, desired, order, order);
    }
};

int main() {
    int val = 42;
    MyAtomic<int> a;
    a._M_ptr = &val;
    int exp = 42;
    // Should call 2-arg overload, which calls 4-arg member, which calls ns_impl returning true
    bool result = a.compare_exchange_weak(exp, 1);
    return result ? 0 : 1;
}
