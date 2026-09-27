#if defined(_MSC_VER)
struct Owner {
	int noThrow(int value);
	int narrow(char value);
};

struct OtherOwner {
	int noThrow(int value);
};

struct Base {
	int noThrow(int value);
};

struct Derived : Base {};

struct OwnerIntSelection {};
struct OwnerCharSelection {};
struct OtherOwnerIntSelection {};
struct BaseMemberSelection {};
struct DerivedMemberSelection {};
struct DerivedOnlyMemberSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

OwnerIntSelection choose(int (Owner::*)(int));
OwnerCharSelection choose(int (Owner::*)(char));
OtherOwnerIntSelection choose(int (OtherOwner::*)(int));
BaseMemberSelection chooseBaseDerived(int (Base::*)(int));
DerivedMemberSelection chooseBaseDerived(int (Derived::*)(int));
DerivedOnlyMemberSelection chooseDerivedOnly(int (Derived::*)(int));

int (Base::* baseMember)(int);

static_assert(SameType<decltype(choose(&Owner::noThrow)), OwnerIntSelection>::value);
static_assert(SameType<decltype(choose(&Owner::narrow)), OwnerCharSelection>::value);
static_assert(SameType<decltype(choose(&OtherOwner::noThrow)), OtherOwnerIntSelection>::value);
static_assert(SameType<decltype(chooseBaseDerived(baseMember)), BaseMemberSelection>::value);
static_assert(SameType<decltype(chooseDerivedOnly(baseMember)), DerivedOnlyMemberSelection>::value);

int main() {
	return 0;
}
#else
// The Itanium mangler does not yet support member-function-pointer parameter
// types. The canonical TypeId planner is covered by CanonicalTypeTests.cpp;
// end-to-end overload declarations remain enabled on MSVC until boundary 3B.
int main() {
	return 0;
}
#endif
