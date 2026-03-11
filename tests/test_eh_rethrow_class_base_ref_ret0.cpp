// Regression test: Windows rethrow must preserve class-type metadata so an
// outer catch(Base&) still matches a rethrown Derived exception.

struct Base {
	virtual ~Base() {}
	int base = 17;
};

struct Derived : Base {
	int extra = 42;
};

void rethrowDerived() {
	try {
		throw Derived{};
	} catch (Derived&) {
		throw;
	}
}

int main() {
	try {
		rethrowDerived();
		return 1;
	} catch (Base& value) {
		return value.base == 17 ? 0 : 2;
	}

	return 3;
}