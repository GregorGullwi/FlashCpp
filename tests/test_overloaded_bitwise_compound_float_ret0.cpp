// A user-defined compound operator on a class may accept a floating-point
// argument. Built-in floating-point rejection must not run before overload
// resolution.
struct Number {
	int value;

	Number& operator&=(float mask) {
		value = mask > 0.0f ? 42 : 0;
		return *this;
	}
};

int main() {
	Number number{0};
	number &= 2.0f;
	return number.value == 42 ? 0 : 1;
}
