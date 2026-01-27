// Minimal test for pointer members in templates

template<typename T>
struct Container {
    T* ptr;
    const T* const_ptr;
};

int main() {
    Container<int> c;
    int x = 42;
    c.ptr = &x;
    c.const_ptr = &x;
    if (*c.ptr != 42) return 1;
    if (*c.const_ptr != 42) return 2;
    return 0;
}
