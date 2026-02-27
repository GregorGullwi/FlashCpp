// Test: variable template partial specialization with dependent initializer
// The initializer expression references template parameters from the partial specialization
// This tests that substituteTemplateParameters is applied to partial specialization initializers
// AND that pattern qualifiers (& / *) are stripped when deducing template parameters

template<typename T>
constexpr int ref_size_v = 0;

// Partial specialization for references - initializer depends on T
template<typename T>
constexpr int ref_size_v<T&> = sizeof(T);

// Pointer-based partial specialization - validates deduction stripping:
// For T*, T should be deduced as int (not int*).
// sizeof(int) == 4, sizeof(int*) == 8 on 64-bit, so this distinguishes correct deduction.
template<typename T>
constexpr int ptr_deref_size_v = -1;

template<typename T>
constexpr int ptr_deref_size_v<T*> = sizeof(T);

int main() {
	// Reference pattern: primary should be 0
	static_assert(ref_size_v<int> == 0, "primary should be 0");
	// Reference pattern: sizeof(int) == 4
	static_assert(ref_size_v<int&> == 4, "int& specialization should give sizeof(int) == 4");

	// Pointer pattern: primary should be -1
	static_assert(ptr_deref_size_v<int> == -1, "primary should be -1");
	// Pointer pattern: T deduced as int (not int*), sizeof(int) == 4
	static_assert(ptr_deref_size_v<int*> == 4, "T should be int, sizeof(int)==4 not sizeof(int*)==8");

	return ref_size_v<int&>;  // Should return 4
}
