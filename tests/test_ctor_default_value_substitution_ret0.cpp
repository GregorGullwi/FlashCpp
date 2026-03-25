// Regression test: constructor parameter default values that depend on
// template parameters must be substituted during instantiation.
// The substituteAndCopyParams helper copies default values; if it forgets
// to run ExpressionSubstitutor on them, template-dependent defaults like
// T{} or T(0) remain unsubstituted and codegen fails or produces wrong results.

// ---- Primary template path (eager constructor instantiation) ----

template<typename T>
struct WithDefault {
	T value;
	// Default value depends on T — must become int{} (== 0) when T=int
	WithDefault(T v = T{}) : value(v) {}
};

// ---- Partial specialization path (substituteAndCopyParams for ctor) ----

template<typename T, typename U>
struct PairDefault {
	T first;
	U second;
	PairDefault(T a, U b = U{}) : first(a), second(b) {}
};

// Partial specialization — constructor default value U{} must be substituted
template<typename T, typename U>
struct PairDefault<T*, U> {
	T deref_first;
	U second;
	PairDefault(T a, U b = U{}) : deref_first(a), second(b) {}
	T get_first() { return deref_first; }
	U get_second() { return second; }
};

// ---- Partial specialization path (substituteAndCopyParams for member func) ----

template<typename T>
struct Maker {
	T make(T fallback = T{}) { return fallback; }
};

template<typename T>
struct Maker<T*> {
	T make(T fallback = T{}) { return fallback; }
};

int main() {
	// Primary template: WithDefault<int> — default ctor arg should be int{} == 0
	WithDefault<int> w1;
	if (w1.value != 0) return 1;

	// Primary template: explicit arg overrides default
	WithDefault<int> w2(42);
	if (w2.value != 42) return 2;

	// Primary template: char default is char{} == '\0' == 0
	WithDefault<char> w3;
	if (w3.value != 0) return 3;

	// Partial specialization: PairDefault<int*, char> — second arg defaults to char{} == 0
	PairDefault<int*, char> p1(10);
	if (p1.get_first() != 10) return 4;
	if (p1.get_second() != 0) return 5;

	// Partial specialization: explicit second arg overrides default
	PairDefault<int*, char> p2(20, 'Z');
	if (p2.get_first() != 20) return 6;
	if (p2.get_second() != 'Z') return 7;

	// Partial specialization member function: Maker<int*>::make() defaults to int{} == 0
	Maker<int*> m1;
	if (m1.make() != 0) return 8;
	if (m1.make(99) != 99) return 9;

	// Primary template member function: Maker<int>::make() defaults to int{} == 0
	Maker<int> m2;
	if (m2.make() != 0) return 10;
	if (m2.make(77) != 77) return 11;

	return 0;
}
