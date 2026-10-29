// ODR-used implicit copy/move constructors of a derived class must still be
// defined when the base is a class-template specialization without a
// constructor AST. This covers direct initialization, by-value calls and
// returns, member and base initialization, and exception handler parameters.
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

struct Holder {
	Derived member;
	Holder(const Derived& source) : member(source) {}
};

struct Child : Derived {
	int extra;
	Child() {}
	Child(const Child& source)
		: Derived(static_cast<const Derived&>(source)), extra(source.extra + 1) {}
};

Derived byValue(Derived source) {
	source.tag += 1;
	return source;
}

template <class T>
Derived templateByValue(Derived source) {
	source.tag += T{1};
	return source;
}

int main() {
	Derived source;
	source.value = 40;
	source.tag = 1;

	Derived copy = source;
	if (copy.value != 40)
		return 1;
	if (copy.tag != 1)
		return 2;

	Derived moved = static_cast<Derived&&>(source);
	if (moved.value != 40)
		return 3;
	if (moved.tag != 1)
		return 4;

	Derived returned = byValue(copy);
	if (returned.value != 40)
		return 5;
	if (returned.tag != 2)
		return 6;

	Derived templated = templateByValue<int>(moved);
	if (templated.value != 40)
		return 7;
	if (templated.tag != 2)
		return 8;

	Holder holder(copy);
	if (holder.member.value != 40)
		return 9;
	if (holder.member.tag != 1)
		return 10;

	Child child_source;
	child_source.value = 42;
	child_source.tag = 7;
	child_source.extra = 5;
	Child child_copy = child_source;
	if (child_copy.value != 42)
		return 11;
	if (child_copy.tag != 7)
		return 12;
	if (child_copy.extra != 6)
		return 13;

	VirtualDerived virtual_source;
	virtual_source.value = 42;
	virtual_source.tag = 9;
	VirtualDerived virtual_copy = virtual_source;
	if (virtual_copy.value != 42)
		return 14;
	if (virtual_copy.tag != 9)
		return 15;

	try {
		Derived thrown;
		thrown.value = 11;
		thrown.tag = 12;
		throw thrown;
	} catch (Derived caught) {
		if (caught.value != 11)
			return 16;
		if (caught.tag != 12)
			return 17;
	}

	return 0;
}
