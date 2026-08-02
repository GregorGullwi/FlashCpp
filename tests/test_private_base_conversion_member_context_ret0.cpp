// A private base is accessible inside members of the derived class.
// C++20 [class.access.base] therefore permits the derived-to-base
// reference conversion performed by asBase().

struct Base {
	long long prefix;
	int value;
};

struct Derived : private Base {
	Derived(int input) {
		prefix = 0x12345678;
		value = input;
	}

	Base& asBase() {
		return *this;
	}
};

int main() {
	Derived object(42);
	Base& base = object.asBase();
	return base.prefix == 0x12345678 && base.value == 42 ? 0 : 1;
}
