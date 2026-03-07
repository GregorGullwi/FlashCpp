// Tests that a global variable used inside a template function body is correctly
// resolved even when the global is declared *after* the template definition.
// At parse time the identifier is Unresolved; at instantiation time the post-
// substitution re-bind (or Unresolved codegen fallback) locates the global.
template<typename T>
T get_late_global() { return late_global; }

int late_global = 42;

int main() {
    return get_late_global<int>() == 42 ? 0 : 1;
}
