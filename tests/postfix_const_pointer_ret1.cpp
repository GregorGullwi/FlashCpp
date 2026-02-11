// Test: postfix const pointer parsing - typename X::Y const* pattern
// Validates that postfix const before pointer works in function parameters

struct MyType {
	using value_type = int;
	using size_type = unsigned long;
};

template<typename T>
int test_func(typename T::value_type const* lhs,
              typename T::size_type lhs_len) {
	if (lhs != nullptr && lhs_len > 0)
		return 1;
	return 0;
}

int main() {
	int val = 42;
	return test_func<MyType>(&val, 1);
}
