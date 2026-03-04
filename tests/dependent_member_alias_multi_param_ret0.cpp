// Test: dependent member alias where the function has more template params than the class template
// Regression test for known issue: resolve_dependent_member_alias passes full function template args
template<typename T>
struct Helper { using type = T; };

// Two template params, but Helper only uses the second
template<typename T, typename U>
typename Helper<U>::type foo(T x, U y) { return y; }

int main() { return foo(3.14, 0); }
