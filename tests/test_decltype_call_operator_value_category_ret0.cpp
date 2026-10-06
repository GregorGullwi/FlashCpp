// [dcl.type.decltype]: a call expression's type follows the value category of
// the expression, and for a callable object that value category is the return
// type of the `operator()` overload selected from the argument value
// categories. The parser must therefore preserve an argument's lvalue category
// while it selects that overload; otherwise `decltype(callable(lvalue))` picks
// the rvalue-reference overload and reports the wrong return type, disagreeing
// with the runtime call. This mixes native scalar arguments with functor and
// value-returning callable structs, including a callable stored as an object
// data member reached through `.`, `->`, and a nested member.

struct Forwarder {
	int& operator()(int& value) { return value; }
	int&& operator()(int&& value) { return static_cast<int&&>(value); }
};

struct ValueCall {
	int operator()(int value) { return value + 1; }
};

struct Inner {
	Forwarder forwarder;
	ValueCall valueCall;
};

struct Holder {
	Forwarder forwarder;
	ValueCall valueCall;
	Inner inner;
};

// Precise reference-kind discriminators. `__is_same` cannot separate two
// reference types, so a partial specialization decides the kind directly.
template <class T> struct IsLValueReference { static constexpr bool value = false; };
template <class T> struct IsLValueReference<T&> { static constexpr bool value = true; };
template <class T> struct IsRValueReference { static constexpr bool value = false; };
template <class T> struct IsRValueReference<T&&> { static constexpr bool value = true; };

int main() {
	int value = 0;
	Forwarder forwarder{};
	ValueCall valueCall{};
	Holder holder{};
	Holder* holderPointer = &holder;

	static_assert(IsLValueReference<decltype(forwarder(value))>::value,
		"an lvalue argument selects the int& overload");
	static_assert(!IsRValueReference<decltype(forwarder(value))>::value,
		"an lvalue argument must not select the int&& overload");
	static_assert(IsRValueReference<decltype(forwarder(static_cast<int&&>(value)))>::value,
		"an xvalue argument selects the int&& overload");
	static_assert(!IsLValueReference<decltype(forwarder(static_cast<int&&>(value)))>::value,
		"an xvalue argument must not select the int& overload");
	static_assert(IsLValueReference<decltype((forwarder)(value))>::value,
		"a parenthesized callable keeps the lvalue overload");
	static_assert(IsLValueReference<decltype(Forwarder{}(value))>::value,
		"a temporary callable keeps the lvalue overload");
	static_assert(IsRValueReference<decltype(Forwarder{}(static_cast<int&&>(value)))>::value,
		"a temporary callable keeps the xvalue overload");
	static_assert(!IsLValueReference<decltype(valueCall(1))>::value,
		"a value-returning call is not a reference");
	static_assert(!IsRValueReference<decltype(valueCall(1))>::value,
		"a value-returning call is not a reference");

	static_assert(IsLValueReference<decltype(holder.forwarder(value))>::value,
		"a member callable keeps the lvalue overload");
	static_assert(!IsRValueReference<decltype(holder.forwarder(value))>::value,
		"a member callable lvalue must not select the int&& overload");
	static_assert(IsRValueReference<decltype(holder.forwarder(static_cast<int&&>(value)))>::value,
		"a member callable keeps the xvalue overload");
	static_assert(IsLValueReference<decltype(holderPointer->forwarder(value))>::value,
		"an arrow member callable keeps the lvalue overload");
	static_assert(IsRValueReference<decltype(holderPointer->forwarder(static_cast<int&&>(value)))>::value,
		"an arrow member callable keeps the xvalue overload");
	static_assert(IsLValueReference<decltype(holder.inner.forwarder(value))>::value,
		"a nested member callable keeps the lvalue overload");
	static_assert(!IsLValueReference<decltype(holder.valueCall(1))>::value,
		"a value-returning member call is not a reference");
	static_assert(!IsRValueReference<decltype(holder.valueCall(1))>::value,
		"a value-returning member call is not a reference");

	int& bound = forwarder(value);
	bound = 7;
	int& memberBound = holder.forwarder(value);
	memberBound = 9;
	return value == 9 ? 0 : 1;
}
