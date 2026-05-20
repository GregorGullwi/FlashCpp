// Regression: this->template get_n<N>() from derived class calling NTTP member
// function template declared in a dependent base class.
// Bug: find_inherited_owner looked up "Base<int>::get_n" but templates are registered
// under "Base::get_n". Fix: fall back to base-template-name lookup and rewrite
// identity.lookup_name to the instantiated form for outer-binding resolution.
template<typename T>
struct Base {
    template<int N>
    int get_n() { return N - N; }
};
template<typename T>
struct Derived : Base<T> {
    int run() { return this->template get_n<42>(); }
};
int main() {
    Derived<int> d;
    return d.run();
}
