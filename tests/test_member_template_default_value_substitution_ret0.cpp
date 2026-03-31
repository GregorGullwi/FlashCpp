// Regression test: member template function parameter default values that
// depend on the OUTER class template parameter must be substituted when
// the class is instantiated.
// The TemplateFunctionDeclarationNode path copies default values verbatim
// at line ~5948 without running ExpressionSubstitutor, so outer-param-
// dependent defaults like T{} remain unsubstituted.

// Class template with a member function template whose parameter default
// depends on the outer class template parameter T.
// When Container<int> is instantiated, the default T{} must become int{} == 0.
template <typename T>
struct Container {
	T stored;

 // Member function template: U is the inner template param (should stay),
 // but the default value for 'fallback' depends on outer param T.
	template <typename U>
	T convert(U input, T fallback = T{}) {
	// If input fits in T, use it; otherwise use fallback
		return static_cast<T>(input) != 0 ? static_cast<T>(input) : fallback;
	}
};

int main() {
	Container<int> ci;
	ci.stored = 10;

 // Call with explicit second arg — should work regardless of default substitution
	if (ci.convert<long long>(42LL, 99) != 42)
		return 1;

 // Call relying on default: T{} should become int{} == 0
 // convert<long long>(0LL) → static_cast<int>(0) != 0 is false → returns fallback = int{} = 0
	if (ci.convert<long long>(0LL) != 0)
		return 2;

 // Non-zero input with default fallback
	if (ci.convert<short>(7, 0) != 7)
		return 3;

	Container<char> cc;
	cc.stored = 'A';

 // char{} == '\0' == 0
	if (ci.convert<char>('\0') != 0)
		return 4;
	if (ci.convert<char>('Z') != static_cast<int>('Z'))
		return 5;

	return 0;
}
