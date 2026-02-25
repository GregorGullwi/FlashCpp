// Test: unnamed template template parameters (valid C++20)
// e.g., template <class, class, template <class> class, template <class> class>
template <class, class, template <class> class, template <class> class>
struct basic_common_reference {};

template <template <class> class Container>
struct Wrapper {};

template <class T>
struct MyVec {};

int main() {
Wrapper<MyVec> w;
return 0;
}
