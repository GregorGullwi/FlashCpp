struct Object {
	int first(int value);
	int first(long value);
	int second(int value);
};

struct IntKey {
	int (Object::*pointer)(int);
	int tag;
};

struct LongKey {
	int (Object::*pointer)(long);
	int tag;
};

constexpr IntKey int_first{&Object::first, 1};
constexpr IntKey int_second{&Object::second, 1};
constexpr LongKey long_first{&Object::first, 1};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<IntKey Value>
struct IntBase {
	short member;
};

template<LongKey Value>
struct LongBase {
	short member;
};

struct Padding {
	long long guard;
};

struct Derived : Padding, IntBase<int_first> {};

static_assert(Same<IntBase<int_first>, IntBase<IntKey{&Object::first, 1}>>::value);
static_assert(!Same<IntBase<int_first>, IntBase<int_second>>::value);
static_assert(Same<LongBase<long_first>, LongBase<LongKey{&Object::first, 1}>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	IntBase<int_first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
