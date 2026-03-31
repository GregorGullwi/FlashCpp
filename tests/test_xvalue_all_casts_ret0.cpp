// Test XValue support across all C++ cast operators
// Verifies that static_cast, const_cast, reinterpret_cast, and dynamic_cast
// all properly mark rvalue reference casts as xvalues

// Simple move helper (equivalent to std::move)
template <typename T>
T&& move(T& arg) {
	return static_cast<T&&>(arg);
}

// Test functions that accept rvalue references (xvalues)
int consume_rvalue(int&& x) {
	return x + 10;
}

// Test functions that accept lvalue references
int consume_lvalue(int& x) {
	return x + 20;
}

// Test class for dynamic_cast
class Base {
public:
	virtual ~Base() {}
	int base_value = 5;
};

class Derived : public Base {
public:
	int derived_value = 7;
};

struct BaseConv {
	int val;
	BaseConv(int v) : val(v) {}
	operator int() const { return val; }
};

struct DerivedConv : BaseConv {
	DerivedConv(int v) : BaseConv(v) {}
	operator int() const { return 99; }
};

int test_static_cast() {
	int value = 5;

	// static_cast to rvalue reference (xvalue)
	int result = consume_rvalue(static_cast<int&&>(value));
	if (result != 15)
		return 1;  // 5 + 10 = 15

	// Using move (which uses static_cast internally)
	result = consume_rvalue(move(value));
	if (result != 15)
		return 2;

	return 0;  // Success
}

int test_const_cast() {
	const int const_value = 8;

	// const_cast to non-const rvalue reference (xvalue)
	// Note: In real code this would be UB to modify, but for testing the cast mechanics
	int result = consume_rvalue(const_cast<int&&>(const_value));
	if (result != 18)
		return 10;  // 8 + 10 = 18

	return 0;  // Success
}

int test_reinterpret_cast() {
	int value = 12;

	// reinterpret_cast to rvalue reference (xvalue)
	int result = consume_rvalue(reinterpret_cast<int&&>(value));
	if (result != 22)
		return 20;  // 12 + 10 = 22

	return 0;  // Success
}

int test_dynamic_cast() {
	Derived derived;
	derived.base_value = 15;

	Base& base_ref = derived;

	// dynamic_cast to rvalue reference (xvalue)
	// This should succeed since derived is actually a Derived object
	Derived&& derived_rvalue = dynamic_cast<Derived&&>(base_ref);

	// Access the derived_value through the xvalue reference
	int result = derived_rvalue.derived_value;
	if (result != 7)
		return 30;

	// Also verify base_value is accessible
	if (derived_rvalue.base_value != 15)
		return 31;

	return 0;  // Success
}

int test_static_cast_base_rvalue_conv_op() {
	DerivedConv derived(10);

	// Cross-type static_cast to rvalue reference should preserve Base identity,
	// so conversion-operator lookup uses Base::operator int() instead of Derived's.
	int result = static_cast<BaseConv&&>(move(derived));
	if (result != 10)
		return 40;

	int direct = derived;
	if (direct != 99)
		return 41;

	return 0;
}

int main() {
	int result;

	// Test 1: static_cast
	result = test_static_cast();
	if (result != 0)
		return result;

	// Test 2: const_cast
	result = test_const_cast();
	if (result != 0)
		return result;

	// Test 3: reinterpret_cast
	result = test_reinterpret_cast();
	if (result != 0)
		return result;

	// Test 4: dynamic_cast
	result = test_dynamic_cast();
	if (result != 0)
		return result;

	// Test 5: cross-type rvalue reference cast preserves target type identity
	result = test_static_cast_base_rvalue_conv_op();
	if (result != 0)
		return result;

	return 0;  // All tests passed!
}
