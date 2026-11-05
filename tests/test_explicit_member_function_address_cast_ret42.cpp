// A target-typed static_cast of an overloaded member-function address must
// retain the selected overload independently for each target signature.
struct Object {
	int first(int) { return 7; }
	int first(double) { return 9; }
};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

static_assert(!Same<
	decltype(static_cast<int (Object::*)(int)>(&Object::first)),
	decltype(static_cast<int (Object::*)(double)>(&Object::first))>::value);

int main() {
	auto int_member = static_cast<int (Object::*)(int)>(&Object::first);
	auto double_member = static_cast<int (Object::*)(double)>(&Object::first);
	Object object;
	if ((object.*int_member)(3) != 7) return 1;
	if ((object.*double_member)(2.5) != 9) return 2;
	return 42;
}
