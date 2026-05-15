struct Base {
	template <typename T>
	int value(T input) {
		return static_cast<int>(input) + 40;
	}
};

struct Derived : Base {
	int test() {
		return this->template value<int>(2);
	}
};

int main() {
	Derived derived;
	return derived.test();
}
