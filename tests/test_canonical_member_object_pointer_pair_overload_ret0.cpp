struct Owner {
	int integer;
	char character;
};

struct OtherOwner {
	int integer;
};

struct Base {
	int value;
};

struct Derived : Base {
	int own;
};

struct OwnerIntegerSelection {};
struct OwnerCharacterSelection {};
struct OtherOwnerIntegerSelection {};
struct BaseIntegerSelection {};
struct DerivedIntegerSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

OwnerIntegerSelection choose(int Owner::*);
OwnerCharacterSelection choose(char Owner::*);
OtherOwnerIntegerSelection choose(int OtherOwner::*);
BaseIntegerSelection choose(int Base::*);
DerivedIntegerSelection choose(int Derived::*);
DerivedIntegerSelection chooseDerivedOnly(int Derived::*);

int Owner::* ownerInteger;
char Owner::* ownerCharacter;
int OtherOwner::* otherOwnerInteger;
int Base::* baseInteger;

static_assert(SameType<decltype(choose(ownerInteger)), OwnerIntegerSelection>::value);
static_assert(SameType<decltype(choose(ownerCharacter)), OwnerCharacterSelection>::value);
static_assert(SameType<decltype(choose(otherOwnerInteger)), OtherOwnerIntegerSelection>::value);
static_assert(SameType<decltype(choose(baseInteger)), BaseIntegerSelection>::value);
static_assert(SameType<decltype(chooseDerivedOnly(baseInteger)), DerivedIntegerSelection>::value);

int main() {
	return 0;
}
