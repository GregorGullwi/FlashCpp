struct Base {
	int value;
};

struct Derived : Base {
};

template <class Owner, int Depth>
struct MemberPointerAliasLayer : MemberPointerAliasLayer<Owner, Depth - 1> {
};

struct MemberPointerAliasRoot {
	using type = int Base::*;
};

template <class Owner>
struct MemberPointerAliasLayer<Owner, 0> : MemberPointerAliasRoot {
};

struct BaseSelection {
	char marker[3];
};

struct DerivedSelection {
	char marker[5];
};

BaseSelection select(int Base::*);
DerivedSelection select(int Derived::*);

using InheritedBaseMemberPointer =
	typename MemberPointerAliasLayer<int, 1024>::type;

static_assert(sizeof(decltype(select(static_cast<InheritedBaseMemberPointer>(nullptr)))) ==
	sizeof(BaseSelection));

int main() {
	return 0;
}
