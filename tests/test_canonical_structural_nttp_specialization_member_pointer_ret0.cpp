template<class T>
struct Object {
	int first;
	int second;
	int method(int value);
	int other(int value);
};

struct FieldKey {
	int Object<int>::* pointer;
	int tag;
};

struct MethodKey {
	int (Object<int>::*pointer)(int);
	int tag;
};

constexpr FieldKey field_first{&Object<int>::first, 1};
constexpr FieldKey field_second{&Object<int>::second, 1};
constexpr MethodKey method_first{&Object<int>::method, 1};
constexpr MethodKey method_second{&Object<int>::other, 1};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<FieldKey Value>
struct FieldBase {
	short member;
};

template<MethodKey Value>
struct MethodBase {
	short member;
};

struct Padding {
	long long guard;
};

struct Derived : Padding, FieldBase<field_first> {};

static_assert(Same<FieldBase<field_first>, FieldBase<FieldKey{&Object<int>::first, 1}>>::value);
static_assert(!Same<FieldBase<field_first>, FieldBase<field_second>>::value);
static_assert(Same<MethodBase<method_first>, MethodBase<MethodKey{&Object<int>::method, 1}>>::value);
static_assert(!Same<MethodBase<method_first>, MethodBase<method_second>>::value);

int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	FieldBase<field_first>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
