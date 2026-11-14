struct Prefix {
	long long guard;
};

struct ValueSource {
	int value;

	template <typename T>
	operator T() const {
		return static_cast<T>(value);
	}
};

struct Middle : virtual ValueSource {
	int padding;
};

struct Derived : Prefix, Middle {
	long long trailing[3];
};

int readShort(short value) {
	return value;
}

int throughMiddle(const Middle& input) {
	return readShort(input);
}

int main() {
	Middle standalone;
	standalone.value = 17;
	Derived derived;
	derived.value = 42;
	if (throughMiddle(standalone) != 17) {
		return 1;
	}
	return throughMiddle(derived) == 42 ? 0 : 2;
}
