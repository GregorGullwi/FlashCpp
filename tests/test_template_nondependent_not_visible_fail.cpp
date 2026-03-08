// C++20 [temp.res]/9: non-dependent names in a template body must be declared
// before the template definition (Phase 1 lookup).  'bar' is non-dependent
// (does not involve T) and is declared *after* the template, so the program is
// ill-formed.  FlashCpp should reject this at template instantiation time.

template<typename T>
T call_bar(T x) { return bar(x); }  // 'bar' not visible at definition time

int bar(int x) { return x + 1; }   // declared too late

int main() {
    return call_bar(4);  // should not compile
}
