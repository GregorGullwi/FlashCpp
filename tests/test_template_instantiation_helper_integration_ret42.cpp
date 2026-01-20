// Integration test for TemplateInstantiationHelper
// This test exercises the ExpressionSubstitutor path via decltype in base class
// Returns 42 on success

// Type traits helper
struct true_type {
	static constexpr bool value = true;
};

struct false_type {
	static constexpr bool value = false;
};

// Template function that will be instantiated through TemplateInstantiationHelper
template<typename T>
true_type type_check_fn();

// Struct that uses decltype in base class - exercises ExpressionSubstitutor
template<typename T>
struct type_checker : decltype(type_check_fn<T>()) { };

int main() {
	// Instantiate type_checker with int
	// This triggers:
	// 1. Parser instantiates type_checker<int>
	// 2. ExpressionSubstitutor substitutes T with int in decltype expression
	// 3. TemplateInstantiationHelper deduces template args (if needed)
	// 4. type_check_fn<int>() is instantiated
	// 5. type_checker<int> inherits from true_type
	
	using checker = type_checker<int>;
	return checker::value ? 42 : 1;
}
