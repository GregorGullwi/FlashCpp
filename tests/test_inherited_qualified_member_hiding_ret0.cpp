struct Base {
	int value = 11;
};

struct Derived : Base {
	int value = 5;

	int readBase() const {
		return Base::value;
	}
};

int main() {
	Derived derived;
	derived.Base::value = 42;
	derived.value = 9;
	return derived.readBase() - 42;
}
