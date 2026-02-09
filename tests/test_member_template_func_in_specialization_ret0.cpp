// Test that member function templates in class template specializations
// don't cause bad_any_cast during template instantiation
namespace std {
template<typename T> struct allocator { using value_type = T; };

template<typename Alloc> struct allocator_traits {
    template<typename Up, typename... Args>
    static void construct(Alloc& a, Up* p, Args&&... args) {}
    
    template<typename Up>
    static void destroy(Alloc& a, Up* p) {}
    
    static typename Alloc::value_type* allocate(Alloc& a, unsigned long n) { return nullptr; }
};

template<typename Tp>
struct allocator_traits<allocator<Tp>> {
    template<typename Up, typename... Args>
    static void construct(allocator<Tp>& a, Up* p, Args&&... args) {}
    
    template<typename Up>
    static void destroy(allocator<Tp>& a, Up* p) {}
    
    static Tp* allocate(allocator<Tp>& a, unsigned long n) { return nullptr; }
};
}

// The key test: instantiating allocator_traits with allocator<int>
// should not crash with bad_any_cast when encountering member function templates
using traits_type = std::allocator_traits<std::allocator<int>>;

int main() {
    // This should compile without bad_any_cast
    std::allocator<int> a;
    int* p = std::allocator_traits<std::allocator<int>>::allocate(a, 10);
    return p == nullptr ? 0 : 1;
}
