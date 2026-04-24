struct Padding {
	long long pad = 0;
};

struct Base {
	int value = 7;
};

struct Derived : Padding, Base {
	int read() const {
		return Base::value;
	}
};

int main() {
	Derived derived;
	derived.value = 42;
	return derived.read() - 42;
}
