template <class T>
struct Base {
	T value;
};

struct Derived : Base<short> {
	int tag;
};

struct VirtualDerived : virtual Base<short> {
	int tag;
};

int main() {
	Derived source;
	source.value = 42;
	source.tag = 7;

	Derived copy = source;
	if (copy.value != 42)
		return 1;
	if (copy.tag != 7)
		return 2;

	Derived move_source;
	move_source.value = 42;
	move_source.tag = 7;
	Derived moved = static_cast<Derived&&>(move_source);
	if (moved.value != 42)
		return 3;
	if (moved.tag != 7)
		return 4;

	VirtualDerived virtual_source;
	virtual_source.value = 42;
	virtual_source.tag = 7;
	VirtualDerived virtual_copy = virtual_source;
	if (virtual_copy.value != 42)
		return 5;
	if (virtual_copy.tag != 7)
		return 6;
	return 0;
}
