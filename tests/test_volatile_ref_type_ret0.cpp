// Test trailing volatile/const qualifiers before reference in type declarations
// Pattern from <functional>: -> __tuple_element_t<_Ind, tuple<_Tp...>> volatile&
template<typename T>
T volatile& get_volatile_ref(T volatile& t) {
    return t;
}

template<typename T>
T const volatile& get_cv_ref(T const volatile& t) {
    return t;
}

int main() {
    volatile int x = 42;
    int volatile& ref = get_volatile_ref(x);
    (void)ref;
    return 0;
}
