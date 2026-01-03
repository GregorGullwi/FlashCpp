// Test type alias with reference modifiers (using type = T&;)
// This tests the fix for parsing member type aliases with & and && modifiers

template<typename T>
struct add_lvalue_reference {
	using type = T&;  // lvalue reference type alias
};

template<typename T>
struct add_rvalue_reference {
	using type = T&&;  // rvalue reference type alias
};

template<typename T>
struct add_pointer {
	using type = T*;  // pointer type alias
};

int main() {
	// Instantiate the templates to verify they compile
	add_lvalue_reference<int> t1;
	add_rvalue_reference<int> t2;
	add_pointer<int> t3;
	
	return 42;
}
