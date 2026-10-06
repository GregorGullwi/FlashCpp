// [dcl.type.decltype]: an unparenthesized class member access names the
// declared type of the member, not the value-category-adjusted type of the
// access expression, while a parenthesized member access still follows the
// lvalue-reference rule. This mixes a native scalar member, a reference member,
// a nested member, and pointer-to-member access so the two rules stay distinct.

struct Inner {
	int value;
};

struct Payload {
	int member;
	int& reference_member;
	Inner inner;
};

// Precise reference-kind discriminators. `__is_same` cannot separate two
// reference types, so a partial specialization decides the kind directly.
template <class T> struct IsLValueReference { static constexpr bool value = false; };
template <class T> struct IsLValueReference<T&> { static constexpr bool value = true; };
template <class T> struct IsRValueReference { static constexpr bool value = false; };
template <class T> struct IsRValueReference<T&&> { static constexpr bool value = true; };

int global_value = 0;

int main() {
	Payload payload{0, global_value, Inner{0}};
	Payload* pointer = &payload;

	static_assert(!IsLValueReference<decltype(payload.member)>::value,
		"an unparenthesized member is the declared int");
	static_assert(!IsRValueReference<decltype(payload.member)>::value,
		"an unparenthesized member is the declared int");
	static_assert(IsLValueReference<decltype(payload.reference_member)>::value,
		"an unparenthesized reference member keeps its declared int&");
	static_assert(!IsLValueReference<decltype(payload.inner.value)>::value,
		"a nested unparenthesized member is the declared int");
	static_assert(!IsRValueReference<decltype(payload.inner.value)>::value,
		"a nested unparenthesized member is the declared int");
	static_assert(!IsLValueReference<decltype(pointer->member)>::value,
		"an arrow unparenthesized member is the declared int");
	static_assert(!IsRValueReference<decltype(pointer->member)>::value,
		"an arrow unparenthesized member is the declared int");

	static_assert(IsLValueReference<decltype((payload.member))>::value,
		"a parenthesized member is an lvalue reference");
	static_assert(IsLValueReference<decltype((payload.inner.value))>::value,
		"a parenthesized nested member is an lvalue reference");
	static_assert(IsLValueReference<decltype((pointer->member))>::value,
		"a parenthesized arrow member is an lvalue reference");

	decltype(payload.member) copy = payload.member;
	payload.member = 5;
	copy = payload.member;
	return copy == 5 ? 0 : 1;
}
