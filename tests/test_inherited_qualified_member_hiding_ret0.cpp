struct Base {
	int value = 42;
};

struct Derived : Base {
	int value = 5;

	int readBase() const {
		return Base::value;
	}
};

int main() {
	Derived derived;
	derived.value = 9;
	return derived.readBase() - 42;
}
