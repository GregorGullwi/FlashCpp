// Test for type alias resolution in base class specifications
// This tests Issue 2 from the architecture plan

// Define base types
struct true_type {
	static constexpr int value = 42;
};

struct false_type {
	static constexpr int value = 0;
};

namespace ns {
struct NamespacedBase {
	static constexpr int value = 42;
};

using AliasBase = NamespacedBase;
} // namespace ns

struct DerivedFromNamespacedAlias : ns::AliasBase {};

// Create type alias
using false_t = false_type;

// Test: Using type alias as base class
// Pattern from <type_traits>: struct is_same : false_type {};
template <typename T, typename U>
struct is_same : false_t {
};

// Specialization that uses true_type directly
template <typename T>
struct is_same<T, T> : true_type {
};

template <typename T, T v>
struct integral_constant {
	static constexpr T val = v;
};

using false_constant = integral_constant<bool, false>;
using true_constant = integral_constant<bool, true>;

struct MyFalseType : false_constant {};
struct MyTrueType : true_constant {};

int main() {
	// Should inherit from false_type (value = 0) since int != char
	is_same<int, char> test1;

	// Should inherit from true_type (value = 42) since int == int
	is_same<int, int> test2;

	if (DerivedFromNamespacedAlias::value != 42)
		return 1;
	if (MyFalseType::val)
		return 2;
	if (!MyTrueType::val)
		return 3;
	if (test1.value != 0)
		return 4;

	return test2.value;	// Should return 42
}
