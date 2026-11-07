// [expr.unary.op], [dcl.type.decltype], [expr.call], [expr.ref], [expr.comma]:
// dereferencing a pointer whose ordered spine is not flat-projectable yields an
// lvalue of the remaining spine, and a call or xvalue argument keeps that
// value category when it selects an overload.

template <class T> struct IsLValueReference { static constexpr bool value = false; };
template <class T> struct IsLValueReference<T&> { static constexpr bool value = true; };
template <class T> struct IsRValueReference { static constexpr bool value = false; };
template <class T> struct IsRValueReference<T&&> { static constexpr bool value = true; };

struct Forwarder {
	int& operator()(int& value) { return value; }
	int&& operator()(int&& value) { return static_cast<int&&>(value); }
};

struct Holder {
	int value;
};

int& lref(int& value) { return value; }
int&& rref(int&& value) { return static_cast<int&&>(value); }

using Row = int[2];

int main() {
	int number = 1;
	Holder holder{0};
	Forwarder forwarder{};
	int* scalar = &number;
	int (*(*grid)[2])[4] = nullptr;
	Row* (*pages)[3] = nullptr;

	static_assert(IsLValueReference<decltype(*scalar)>::value, "a scalar dereference is an lvalue");
	static_assert(IsLValueReference<decltype(*grid)>::value, "a non-projectable dereference is an lvalue");
	static_assert(!IsRValueReference<decltype(*grid)>::value, "a non-projectable dereference is not an xvalue");
	static_assert(IsLValueReference<decltype(*pages)>::value, "an array of pointers to an array alias is an lvalue");
	static_assert(IsLValueReference<decltype(holder.value)>::value == false, "an unparenthesized member keeps its declared type");
	static_assert(!IsRValueReference<decltype(holder.value)>::value, "an unparenthesized member is not an xvalue");
	static_assert(IsLValueReference<decltype((holder.value))>::value, "a parenthesized member of an lvalue is an lvalue");
	static_assert(IsRValueReference<decltype((static_cast<Holder&&>(holder).value))>::value,
		"a member of an xvalue is an xvalue");
	static_assert(IsLValueReference<decltype(forwarder(holder.value))>::value, "an lvalue member selects the lvalue overload");
	static_assert(IsRValueReference<decltype(forwarder(static_cast<Holder&&>(holder).value))>::value,
		"an xvalue member selects the xvalue overload");
	static_assert(IsLValueReference<decltype(forwarder((0, number)))>::value, "a comma lvalue selects the lvalue overload");
	static_assert(IsRValueReference<decltype(forwarder((0, static_cast<int&&>(number))))>::value,
		"a comma xvalue selects the xvalue overload");
	static_assert(IsLValueReference<decltype(forwarder(lref(number)))>::value, "a call returning T& is an lvalue argument");
	static_assert(IsRValueReference<decltype(forwarder(rref(static_cast<int&&>(number))))>::value,
		"a call returning T&& is an xvalue argument");
	static_assert(IsLValueReference<decltype(lref(number))>::value, "decltype of a call returning T& is T&");
	static_assert(IsRValueReference<decltype(rref(static_cast<int&&>(number)))>::value, "decltype of a call returning T&& is T&&");
	static_assert(IsLValueReference<decltype((number = 4))>::value, "assignment is an lvalue");
	static_assert(IsLValueReference<decltype(0, number)>::value, "a comma expression keeps the lvalue operand");
	static_assert(!IsLValueReference<decltype(number)>::value && !IsRValueReference<decltype(number)>::value,
		"an unparenthesized id keeps the declared type");

	if (sizeof(*grid) != 2 * sizeof(void*))
		return 1;
	if (sizeof((*grid)[0]) != sizeof(void*))
		return 2;
	if (sizeof(*(*grid)[0]) != 4 * sizeof(int))
		return 3;
	if (sizeof(*pages) != 3 * sizeof(void*))
		return 4;
	if (sizeof((*pages)[0]) != sizeof(void*))
		return 5;
	if (sizeof(*(*pages)[0]) != 2 * sizeof(int))
		return 6;

	int& selected = forwarder((0, number));
	selected = 7;
	return number == 7 ? 0 : 7;
}
