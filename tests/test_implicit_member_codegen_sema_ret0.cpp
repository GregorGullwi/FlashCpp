struct Pad {
	long long padding = 7;
};

struct Base {
	int value = 42;
};

struct Derived : Pad, Base {
	int direct() {
		return value;
	}

	int throughThisLambda() {
		auto read = [this]() {
			return value;
		};
		return read();
	}

};

int main() {
	Derived d;
	return (d.direct() == 42 &&
			d.throughThisLambda() == 42)
		? 0
		: 1;
}
