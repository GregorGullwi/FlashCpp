struct Key {
	float small;
	double wide;
	float row[2];
};

constexpr Key key{1.5f, 2.25, {3.5f, 4.5f}};

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

struct Derived : Padding, Base<key> {};

static_assert(Same<Base<key>, Base<Key{1.5f, 2.25, {3.5f, 4.5f}}>>::value);
static_assert(!Same<Base<key>, Base<Key{2.5f, 2.25, {3.5f, 4.5f}}>>::value);
static_assert(!Same<Base<key>, Base<Key{1.5f, 2.5, {3.5f, 4.5f}}>>::value);
static_assert(!Same<Base<key>, Base<Key{1.5f, 2.25, {3.5f, 5.5f}}>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
