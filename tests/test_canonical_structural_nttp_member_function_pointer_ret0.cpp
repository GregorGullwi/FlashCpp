struct Object {
	int first(int value);
	int second(int value);
};

struct Key {
	int (Object::*pointer)(int);
	int tag;
};

constexpr Key first{&Object::first, 1};
constexpr Key second{&Object::second, 1};

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

struct Padding {
	long long guard;
};

struct Derived : Padding, Base<first> {};

static_assert(Same<Base<first>, Base<Key{&Object::first, 1}>>::value);
static_assert(!Same<Base<first>, Base<second>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
