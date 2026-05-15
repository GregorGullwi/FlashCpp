// Two-phase lookup for member function template bodies:
// Non-dependent calls inside a member function template body should be resolved
// at the point of DEFINITION, not the point of instantiation.
//
// C++20 [temp.res]/9: a name used in a template definition is bound at the
// time of template definition for ordinary (non-ADL) unqualified lookup when
// the name is not dependent.
//
// Here f(int) is declared *after* the class template, but the non-dependent
// call f(0) inside Wrapper<T>::call_f() must see only the definition-time
// overload f(long) and return 42, not 7.
//
// Although '0' has type int (which is an exact match for f(int) when both are
// visible), two-phase lookup restricts the candidate set at template definition
// time to only f(long).  The implicit int->long conversion is used, and the
// function returns 42.
//
// Clang verifies: the program exits with 42.

int f(long) { return 42; }  // visible at template definition time

template <typename T>
struct Wrapper {
    template <typename U>
    int call_f(U) {
        return f(0);  // non-dependent call -- must bind to f(long) above
    }
};

int f(int) { return 7; }  // declared after the template -- must NOT be picked

int main() {
    Wrapper<double> w;
    return w.call_f(0);  // should return 42 (two-phase lookup picks f(long))
}
