// Regression test: zero-parameter function templates with explicit type arguments
// must be evaluable in constexpr context (Bug fix: explicit type args ignored
// in evaluate_function_call for TemplateFunctionDeclarationNode).
//
// Also tests: no infinite recursion in ExpressionSubstitutor when
// materializeStoredTemplateArgs and substituteQualifiedIdentifier form a cycle.

template<typename T>
struct is_int {
	static constexpr bool value = false;
};
template<>
struct is_int<int> {
	static constexpr bool value = true;
};

template<typename T>
constexpr bool check_is_int() noexcept {
	if constexpr (is_int<T>::value) return true;
	return false;
}

template<typename T>
struct Checker {
	static_assert(check_is_int<T>(), "T must be int");
};
template struct Checker<int>;

struct DummyType {};
template<typename T>
struct Wrapper {};

template<int N>
constexpr int valueOf() noexcept {
	return N;
}

// Explicit function template call with type args (no function params)
static_assert(check_is_int<int>(), "int should pass");
static_assert(!check_is_int<double>(), "double should fail");
static_assert(!check_is_int<short>(), "short should fail");
static_assert(!check_is_int<long long>(), "long long should fail");
static_assert(!check_is_int<unsigned int>(), "unsigned int should fail");
static_assert(!check_is_int<DummyType>(), "struct type should fail");
static_assert(!check_is_int<Wrapper<int>>(), "template-instantiation type should fail");
static_assert(valueOf<42>() == 42, "explicit NTTP should instantiate correctly");

int main() {
	return 0;
}
