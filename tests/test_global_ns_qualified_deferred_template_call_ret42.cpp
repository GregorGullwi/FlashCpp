// Test: global-scope qualified call ::ns::f(t) with dependent argument should
// defer instantiation (not eagerly bind/fail) during template body parsing.
namespace ops {
template<typename T>
T multiply(T a, T b) { return a * b; }
}

template<typename T>
T globalQualifiedMul(T x, T y) {
return ::ops::multiply(x, y);
}

int main() {
return globalQualifiedMul(6, 7);  // 42
}
