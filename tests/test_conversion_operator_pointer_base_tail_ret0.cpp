struct Prefix {
	long long padding;
};

template <class T>
struct Base {
	T value;
};

struct Fixed : Prefix, Base<short> {};
struct Virtual : Prefix, virtual Base<int> {};

struct FixedSource {
	Fixed* pointer;
	operator Fixed*() const { return pointer; }
};

struct VirtualSource {
	Virtual* pointer;
	operator Virtual*() const { return pointer; }
};

Virtual* passVirtualPointer(Virtual* pointer) {
	return pointer;
}

bool checkFixed(const Base<short>* actual, const Base<short>* expected) {
	if (actual != expected) {
		return false;
	}
	if (!actual) {
		return true;
	}
	return actual->value == 42;
}

bool checkVirtual(const Base<int>* actual, const Base<int>* expected) {
	if (actual != expected) {
		return false;
	}
	if (!actual) {
		return true;
	}
	return actual->value == 73;
}

int main() {
	Fixed fixed{};
	fixed.value = 42;
	Virtual virtual_base{};
	virtual_base.value = 73;
	const Base<short>* expected_fixed = static_cast<Base<short>*>(&fixed);
	const Base<int>* expected_virtual = static_cast<Base<int>*>(&virtual_base);
	const Base<short>* null_fixed = nullptr;
	const Base<int>* null_virtual = nullptr;
	if (!checkFixed(FixedSource{&fixed}, expected_fixed)) {
		return 1;
	}
	if (!checkFixed(FixedSource{nullptr}, null_fixed)) {
		return 2;
	}
	if (!checkVirtual(VirtualSource{passVirtualPointer(&virtual_base)}, expected_virtual)) {
		return 3;
	}
	if (!checkVirtual(VirtualSource{nullptr}, null_virtual)) {
		return 4;
	}
	return 0;
}
