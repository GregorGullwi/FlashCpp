// Test: Conversion operators returning reference types
// Validates: operator _Tp&() and operator _Tp*() syntax

template<typename _Tp>
struct reference_wrapper {
    _Tp* _M_data;

    operator _Tp&() const noexcept { return *_M_data; }
};

template<typename _Tp>
struct pointer_wrapper {
    _Tp _M_data;

    operator _Tp*() { return &_M_data; }
};

int main() {
    int x = 42;
    reference_wrapper<int> rw{&x};
    return 0;
}
