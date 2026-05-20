// Regression: this->template as<U>() from derived class calling type-param member
// function template declared in a dependent base class.
// Bug: find_inherited_owner looked up "Base<int>::as" but templates are registered
// under "Base::as". Fix: fall back to base-template-name lookup and rewrite
// identity.lookup_name to the instantiated form for outer-binding resolution.
template<typename T>
struct Base {
    template<typename U>
    U as() { return static_cast<U>(0); }
};
template<typename T>
struct Derived : Base<T> {
    int run() { return this->template as<int>(); }
};
int main() {
    Derived<int> d;
    return d.run();
}
