// Test: template function with struct braced-init default argument (multi-element)
// Exercises the appendMissingDefaultArguments lambda in the template instantiation
// path (Parser_Expr_PrimaryExpr.cpp). That lambda does not convert multi-element
// InitializerListNode defaults to ConstructorCallNode for struct parameters,
// unlike the overload resolution path which handles this correctly.

struct Vec {
	int a;
	int b;
};

template <typename T>
T addVec(T val, Vec v = {20, 22}) {
	return val + v.a + v.b;
}

int main() {
	// Call with explicit template args and omit the struct default.
	// This takes the try_instantiate_template_explicit path, which uses
	// appendMissingDefaultArguments to fill in the Vec{20, 22} default.
	int r1 = addVec<int>(0);	 // 0 + 20 + 22 = 42
	if (r1 != 42)
		return 1;

	// Verify explicit override still works
	Vec v;
	v.a = 10;
	v.b = 32;
	int r2 = addVec<int>(0, v); // 0 + 10 + 32 = 42
	if (r2 != 42)
		return 2;

	return 42;
}
