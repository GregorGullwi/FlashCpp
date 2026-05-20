// Regression: this->template member<N>() through two levels of dependent base classes.
// Verifies that find_inherited_owner's template-instantiation fallback works recursively
// even when the member template is in a grandparent dependent class.
template<typename T>
struct GrandBase {
    template<int N>
    int offset() { return N - N; }
};
template<typename T>
struct MiddleBase : GrandBase<T> {};
template<typename T>
struct Derived : MiddleBase<T> {
    int run() { return this->template offset<7>(); }
};
int main() {
    Derived<int> d;
    return d.run();
}
