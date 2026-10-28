struct Base {
	int first;
	int second;
};

struct Middle : Base {
	int third;
};

struct Object : Middle {
	int fourth;
};

struct Key {
	int Object::* pointer;
	int tag;
};

constexpr Key first{&Object::first, 1};
constexpr Key second{&Object::second, 1};
constexpr Key fourth{&Object::fourth, 1};

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

struct Padding {
	long long guard;
};

struct Derived : Padding, BaseHolder<first> {};

static_assert(Same<BaseHolder<first>, BaseHolder<Key{&Object::first, 1}>>::value);
static_assert(!Same<BaseHolder<first>, BaseHolder<second>>::value);
static_assert(!Same<BaseHolder<first>, BaseHolder<fourth>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	BaseHolder<first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
