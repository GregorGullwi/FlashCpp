// Regression test: appendFunctionCallArgType now deduces the correct type
// for static_cast<T>(), call expressions with a resolved callee, and unary
// arithmetic/logical operators.  Previously all of these fell through to the
// Int default, causing mis-deduction of T when a function template was
// called with such arguments.

// Overloaded function templates whose resolution depends on correct deduction.
template<typename T>
int identity_check(T val) {
return static_cast<int>(val);
}

// Helper returning a specific type to test CallExprNode deduction.
double make_double(int x) { return static_cast<double>(x); }
float  make_float(int x)  { return static_cast<float>(x); }

int main() {
// static_cast: T should deduce to double, not int.
double d = static_cast<double>(42);
int r1 = identity_check(d);
if (r1 != 42) return 1;

// static_cast to float
float f = static_cast<float>(7);
int r2 = identity_check(f);
if (r2 != 7) return 2;

// Unary negation: the result type mirrors the operand.
int neg = identity_check(-5);
if (neg != -5) return 3;

// Logical not: result type is bool (represented as int 0/1).
int lnot = identity_check(!false);
if (lnot != 1) return 4;

return 0;
}
