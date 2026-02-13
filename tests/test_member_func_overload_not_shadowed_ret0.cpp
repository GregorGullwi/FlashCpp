// Test: partial specialization member functions should generate code correctly.
// When a class template has a partial specialization with member functions,
// instantiation should find the matching pattern and generate code for its members.
template<typename T, bool = false>
struct MyAtomic;

template<typename T>
struct MyAtomic<T, false> {
    T value;
    
    bool cmpxchg_weak(T& expected, T desired, int success, int failure) const {
        return false;
    }
};

int main() {
    MyAtomic<int> a;
    a.value = 42;
    int exp = 0;
    a.cmpxchg_weak(exp, 1, 0, 0);
    return 0;
}
