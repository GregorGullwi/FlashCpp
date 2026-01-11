// Test template template parameter with variadic pack
// Pattern from <type_traits> line 2727:
// template<typename _Def, template<typename...> class _Op, typename... _Args>
// struct __detected_or

// Primary template
template<typename T>
struct Container {
    T value;
};

// Template template parameter with variadic pack: template<typename...> class Op
template<typename Def, template<typename...> class Op, typename... Args>
struct detected_or {
    using type = Def;
};

int main() {
    detected_or<int, Container, double>::type x = 42;
    return x;
}
