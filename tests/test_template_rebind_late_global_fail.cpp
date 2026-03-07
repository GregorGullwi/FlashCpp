// C++20 [temp.res]/9: 'late_global' is a non-dependent name used inside a
// template body, but it is declared *after* the template definition.  This is
// ill-formed; the declaration must be visible at the point of the template.
// Previously FlashCpp accepted this via post-substitution rebinding (MSVC-like
// non-standard behaviour).  This test documents the correct rejection.

template<typename T>
T get_late_global() { return late_global; }

int late_global = 42;

int main() {
    return get_late_global<int>() == 42 ? 0 : 1;
}
