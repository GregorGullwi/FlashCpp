// Test constexpr destructor support in template struct contexts.
// C++20 allows destructors to be constexpr.

template<typename T>
struct Holder {
	T value;
	constexpr explicit Holder(T v) : value(v) {}
	constexpr ~Holder() {}
};

constexpr int test_holder_int() {
	Holder<int> h(42);
	return h.value;
}
static_assert(test_holder_int() == 42, "Template holder<int> with constexpr dtor");

constexpr double test_holder_double() {
	Holder<double> h(3.14);
	return h.value;
}
static_assert(test_holder_double() == 3.14, "Template holder<double> with constexpr dtor");

// Template with non-trivial constexpr destructor body
template<typename T>
struct Resetter {
	T val;
	constexpr Resetter(T v) : val(v) {}
	constexpr ~Resetter() { val = T{}; }
};

constexpr int test_resetter_return_before_dtor() {
	Resetter<int> r(99);
	return r.val; // Captures 99 before ~Resetter() zeroes it
}
static_assert(test_resetter_return_before_dtor() == 99,
	"Return value captured before constexpr destructor runs");

// Multiple template objects in scope
constexpr int test_multi_template_scope() {
	Holder<int> a(10);
	Holder<int> b(20);
	return a.value + b.value;
}
static_assert(test_multi_template_scope() == 30,
	"Multiple template objects with constexpr dtors");

// Nested template struct with constexpr destructor
template<typename T>
struct Outer {
	Holder<T> inner;
	constexpr Outer(T v) : inner(v) {}
	constexpr ~Outer() {}
};

constexpr int test_nested_template() {
	Outer<int> o(7);
	return o.inner.value;
}
static_assert(test_nested_template() == 7,
	"Nested template struct with constexpr destructors");

int main() { return 0; }
