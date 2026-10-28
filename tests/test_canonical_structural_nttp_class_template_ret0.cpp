template<class T>
struct Key {
	T value;
};

constexpr Key<int> key{42};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<Key<int> Value>
struct Base {
	short member;
};

struct Padding {
	long long guard;
};

struct Derived : Padding, Base<key> {};

static_assert(Same<Base<key>, Base<Key<int>{42}>>::value);
static_assert(!Same<Base<key>, Base<Key<int>{43}>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
