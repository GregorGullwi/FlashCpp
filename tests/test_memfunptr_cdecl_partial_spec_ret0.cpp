// Test: member function pointer partial specialization with __cdecl calling convention
// This pattern is used heavily in MSVC's <type_traits> header (_Is_memfunptr).
template <class _Ty>
struct _Is_memfunptr {
    static constexpr bool value = false;
};

template <class _Ret, class _Arg0, class... _Types>
struct _Is_memfunptr<_Ret (__cdecl _Arg0::*)(_Types...)> {
    static constexpr bool value = true;
};

struct MyClass {
    int foo(int x) { return x; }
};

int main() {
    // _Is_memfunptr<int (MyClass::*)(int)> should match the __cdecl specialization
    // and have value == true; the primary template has value == false.
    // Return 0 if the specialization was correctly selected.
    return _Is_memfunptr<int (MyClass::*)(int)>::value ? 0 : 1;
}
