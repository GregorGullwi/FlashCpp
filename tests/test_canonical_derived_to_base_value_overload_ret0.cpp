struct Base {};
struct Derived : Base {};
struct VirtualDerived : virtual Base {};

struct ConstructedFromDerived {
	ConstructedFromDerived(const Derived&) {}
};

struct BaseSelection {};
struct UserDefinedSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

BaseSelection select(Base);
UserDefinedSelection select(ConstructedFromDerived);

constexpr bool selects_base_for_derived =
	SameType<decltype(select(Derived{})), BaseSelection>::value;
constexpr bool selects_base_for_virtual_derived =
	SameType<decltype(select(VirtualDerived{})), BaseSelection>::value;

static_assert(selects_base_for_derived);
static_assert(selects_base_for_virtual_derived);

int main() {
	return selects_base_for_derived && selects_base_for_virtual_derived ? 0 : 1;
}
