// Test __alignof__ operator support (GCC/Clang extension)
// This pattern appears in <type_traits> at line 2180

struct alignas(8) Aligned8 {
    int x;
};

struct MyType {
    char c;
    int i;
};

// Test __alignof__ with typename in template context
template<typename T>
struct Wrapper {
    using type = T;
};

template<typename T, 
         unsigned long Align = __alignof__(typename Wrapper<T>::type)>
struct WithAlignment {
    static constexpr unsigned long alignment = Align;
};

int main() {
    // Test basic __alignof__
    unsigned long a1 = __alignof__(int);
    unsigned long a2 = __alignof__(Aligned8);
    unsigned long a3 = __alignof__(MyType);
    
    // Test with template (though instantiation may not work yet)
    // WithAlignment<int> w;
    
    return 0;
}
