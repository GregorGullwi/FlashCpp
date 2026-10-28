struct Base {
	virtual int first(int value) { return value; }
	virtual int second(int value) { return value; }
};

struct Middle : Base {};

struct Key {
	int (Middle::*pointer)(int);
	int tag;
};

struct DirectKey {
	int (Base::*pointer)(int);
	int tag;
};

constexpr Key first{&Middle::first, 1};
constexpr Key second{&Middle::second, 1};
constexpr DirectKey direct_first{&Base::first, 1};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<Key Value>
struct BaseHolder {
	short member;
};

template<DirectKey Value>
struct DirectHolder {
	short member;
};

struct Padding {
	long long guard;
};

struct Derived : Padding, BaseHolder<first> {};

static_assert(Same<BaseHolder<first>, BaseHolder<Key{&Middle::first, 1}>>::value);
static_assert(!Same<BaseHolder<first>, BaseHolder<second>>::value);
static_assert(Same<DirectHolder<direct_first>, DirectHolder<DirectKey{&Base::first, 1}>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	BaseHolder<first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
