// Test: Global-scope-qualified types in type specifier
// Verifies parsing of ::Type pattern used in headers like <vector>
// The :: prefix denotes global namespace scope resolution
struct Widget {
    int value;
};

// This function parameter uses the :: global scope prefix
void set_widget(::Widget& w, int v) {
    w.value = v;
}

int main() {
    Widget w;
    w.value = 0;
    set_widget(w, 42);
    return w.value;
}
