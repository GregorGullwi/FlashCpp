// using Base::operator= restores the nominated base assignment overload after a
// derived assignment operator hides the base name.
struct NonConvertible {
	NonConvertible() = delete;
};

struct Base {
	int value = 0;
	Base& operator=(int input) {
		value = input;
		return *this;
	}
};

struct Derived : Base {
	using Base::operator=;
	Derived& operator=(NonConvertible) { return *this; }
};

int main() {
	Derived value;
	value = 7;
	return value.value == 7 ? 0 : 1;
}
