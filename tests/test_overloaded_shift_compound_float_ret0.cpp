// A user-defined shift assignment may accept a floating-point argument.
// Built-in validation must not run before overload resolution.
struct Number {
	int value;

	Number& operator>>=(double shift) {
		value = shift > 0.0 ? 42 : 0;
		return *this;
	}
};

int main() {
	Number number{0};
	number >>= 3.0;
	return number.value == 42 ? 0 : 1;
}
