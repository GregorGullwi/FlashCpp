// Regression: sema owns the implicit default-constructor decision and the
// base/member initialization plan consumed by IR. Exercise a template base,
// differently sized members, a class member, and a bitfield initializer.
template<typename T>
struct Base {
	T base_value = static_cast<T>(7);
};

struct Member {
	short value = 11;
};

template<typename T>
struct Derived : Base<T> {
	Member members[2];
	unsigned bits : 4 = 3;
	char tail = 10;
};

int main() {
	Derived<int> value;
	return value.base_value + value.members[0].value + value.members[1].value +
		value.bits + value.tail;
}
