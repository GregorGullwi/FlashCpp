// A class with an implicit or explicitly-defaulted default constructor is
// default-constructible only when its base classes and its initialized members
// are themselves default-constructible. A user-provided default constructor
// initializes those subobjects itself and does not impose the requirement.
struct MemberNoDefault {
	MemberNoDefault(int) {}
};

struct BaseNoDefault {
	BaseNoDefault(int) {}
};

struct BaseGood {
	int value;
};

struct HasBadMember {
	MemberNoDefault member;
};

struct HasBadBase : BaseNoDefault {
};

struct HasGoodBase : BaseGood {
	long tag;
};

struct UserProvidesMember {
	MemberNoDefault member;
	UserProvidesMember() : member(0) {}
};

struct HasReference {
	int& reference;
};

struct NestedBad {
	HasBadMember inner;
};

template <class Type>
struct Box {
	Type value;
};

static_assert(!__is_constructible(HasBadMember), "member without a default constructor");
static_assert(!__is_constructible(HasBadBase), "base without a default constructor");
static_assert(__is_constructible(HasGoodBase), "usable base");
static_assert(__is_constructible(UserProvidesMember), "user-provided constructor initializes the member");
static_assert(!__is_constructible(HasReference), "reference member without an initializer");
static_assert(!__is_constructible(NestedBad), "nested member without a default constructor");
static_assert(__is_constructible(Box<int>), "class-template specialization with a scalar member");

int main() {
	return 0;
}
