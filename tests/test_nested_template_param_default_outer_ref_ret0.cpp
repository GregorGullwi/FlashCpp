// Test: nested template parameter defaults that reference outer template parameters
// Exercises the code path in parse_template_parameter_list where outer template
// parameter names must remain visible while parsing inner parameter defaults.
// Regression test for the ScopedState CopyOnSave fix.

template <typename T>
struct wrapper {
	using type = T;
};

// Inner member template's default references outer class template parameter T
template <typename T>
struct Outer {
	template <typename U = typename wrapper<T>::type>
	U get() { U val{}; return val; }
};

// Same pattern but with a non-type default referencing outer param via sizeof
template <typename T>
struct SizedOuter {
	template <int N = sizeof(T)>
	int size() { return N; }
};

int main() {
	Outer<int> o;
	int v = o.get();  // U defaults to wrapper<int>::type = int

	SizedOuter<int> so;
	int s = so.size();  // N defaults to sizeof(int) = 4

	return v + (s - 4);  // 0 + 0 = 0
}
