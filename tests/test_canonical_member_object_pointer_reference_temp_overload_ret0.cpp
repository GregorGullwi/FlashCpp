struct Base {
	int member;
};

struct Derived : Base {};

struct BaseSelection {};
struct DerivedSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

BaseSelection choose(int Base::* const&);
DerivedSelection choose(int Derived::* const&);
DerivedSelection chooseDerivedOnly(int Derived::* const&);

int Base::* baseMember;

static_assert(SameType<decltype(choose(baseMember)), BaseSelection>::value);
static_assert(SameType<
	decltype(chooseDerivedOnly(baseMember)), DerivedSelection>::value);

int main() {
	return 0;
}
