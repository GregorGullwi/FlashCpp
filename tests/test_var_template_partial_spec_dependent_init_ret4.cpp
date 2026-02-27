// Test: variable template partial specialization with dependent initializer
// The initializer expression references template parameters from the partial specialization
// This tests that substituteTemplateParameters is applied to partial specialization initializers

template<typename T>
constexpr int ref_size_v = 0;

// Partial specialization for references - initializer depends on T
template<typename T>
constexpr int ref_size_v<T&> = sizeof(T);

int main() {
	// Primary template: should be 0
	static_assert(ref_size_v<int> == 0, "primary should be 0");
	// Partial specialization: sizeof(int) == 4
	static_assert(ref_size_v<int&> == 4, "int& specialization should give sizeof(int) == 4");
	return ref_size_v<int&>;  // Should return 4
}
