// Test that calling a consteval member function with a non-constant argument
// is ill-formed.  C++20 [dcl.consteval]: every call to a consteval function
// must be a constant expression.

struct Calc {
	consteval int triple(int v) const { return v * 3; }
};

int main() {
	Calc c;
	int r = 7; // non-constant
	return c.triple(r); // ERROR: not a constant expression
}
