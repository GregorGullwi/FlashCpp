struct Owner {
	int noThrow(int value);
	int narrow(char value);
};

struct OtherOwner {
	int noThrow(int value);
};

struct OwnerIntSelection {};
struct OwnerCharSelection {};
struct OtherOwnerIntSelection {};

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

static_assert(SameType<decltype(choose(&Owner::noThrow)), OwnerIntSelection>::value);
static_assert(SameType<decltype(choose(&Owner::narrow)), OwnerCharSelection>::value);
static_assert(SameType<decltype(choose(&OtherOwner::noThrow)), OtherOwnerIntSelection>::value);

int main() {
	return 0;
}
