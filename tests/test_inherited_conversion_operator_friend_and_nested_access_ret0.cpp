struct ValueBase {
	operator int() const {
		return 7;
	}
};

struct Derived : private ValueBase {
	friend int read(Derived value) {
		return value;
	}

	struct Nested {
		static int read(Derived value) {
			return value;
		}
	};
};

int main() {
	Derived value;
	if (read(value) != 7) {
		return 1;
	}
	return Derived::Nested::read(value) == 7 ? 0 : 2;
}
