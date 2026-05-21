// Test: Out-of-line class-template constructor body replay preserves
// definition-context lookup for non-dependent unqualified names.

int select(long) { return 33; }

template<typename T>
struct Widget {
    int value;
    Widget();
};

template<typename T>
Widget<T>::Widget() : value(select(0)) {
    // Non-dependent call in ctor initializer: should bind to select(long)
}

int select(int) { return 44; }  // Declared after definition, should not be considered

int main() {
    Widget<int> w;
    return w.value == 33 ? 0 : 1;
}
