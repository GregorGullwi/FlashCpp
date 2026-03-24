// Test const_cast inside constexpr function bodies that reference local variables
// and function parameters.  This exercises the bindings-aware const_cast evaluation
// path (ConstExprEvaluator_Members.cpp evaluate_expression_with_bindings_dispatch).

// const_cast on a function parameter (reference)
constexpr int cast_param(const int& x) {
	return const_cast<int&>(x);
}

// const_cast on a local variable
constexpr int cast_local() {
	const int local = 77;
	return const_cast<int&>(local);
}

// const_cast on a pointer to a local variable
constexpr int cast_local_ptr() {
	const int val = 55;
	const int* p = &val;
	return *const_cast<int*>(p);
}

// const_cast inside a constexpr function that also has mutable local state
// (ensures the mutable-bindings path correctly falls through to the const_cast handler)
constexpr int cast_with_mutation() {
	int sum = 0;
	const int a = 10;
	sum += const_cast<int&>(a);
	const int b = 20;
	sum += const_cast<int&>(b);
	return sum;
}

static_assert(cast_param(42) == 42);
static_assert(cast_local() == 77);
static_assert(cast_local_ptr() == 55);
static_assert(cast_with_mutation() == 30);

int main() {
	return 0;
}
