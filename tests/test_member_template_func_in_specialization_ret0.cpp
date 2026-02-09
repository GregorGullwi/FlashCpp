// Test that member function templates in class template specializations
// don't cause bad_any_cast during template instantiation
// The key test is that this compiles without crashing

template<typename Alloc> struct alloc_traits {
    template<typename Up>
    static int construct(Up*) { return 0; }
};

template<typename T> struct my_alloc { using value_type = T; };

template<typename Tp>
struct alloc_traits<my_alloc<Tp>> {
    template<typename Up>
    static int construct(Up*) { return 0; }
};

int main() {
    return 0;
}
