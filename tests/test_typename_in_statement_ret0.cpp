// Regression test: typename keyword in statement/declaration context
// typename-qualified variable declarations must parse in function bodies

template<typename T>
struct Container {
    using value_type = T;
    value_type val;
};

template<typename C>
int getVal(C& c) {
    typename C::value_type result = c.val;
    return result;
}

int main() {
    Container<int> c;
    c.val = 42;
    return getVal(c) - 42;  // 0
}
