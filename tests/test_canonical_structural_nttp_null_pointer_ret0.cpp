struct Key {
	int* pointer;
	int tag;
};

constexpr Key first{nullptr, 1};
constexpr Key second{nullptr, 2};

struct Object {
	int member;
};

struct MemberKey {
	int Object::* pointer;
	int tag;
};

constexpr MemberKey no_member{nullptr, 3};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<Key Value>
struct Base {
	short member;
};

template<MemberKey Value>
struct MemberBase {
	short member;
};

struct Padding {
	long long guard;
};

struct Derived : Padding, Base<first> {};

static_assert(Same<Base<first>, Base<Key{nullptr, 1}>>::value);
static_assert(!Same<Base<first>, Base<second>>::value);
static_assert(Same<MemberBase<no_member>, MemberBase<MemberKey{nullptr, 3}>>::value);
static_assert(!Same<MemberBase<no_member>, MemberBase<MemberKey{nullptr, 4}>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
