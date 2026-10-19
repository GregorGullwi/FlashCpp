template <class T>
struct Widened {
	long long signed_value;
	unsigned long long unsigned_value;
	double floating_value;
	Widened(T input, unsigned short other) : signed_value(input), unsigned_value{other}, floating_value(input) {}
};

struct Defaults {
	long long value = short(-19);
	Defaults() {}
};

int main() {
	Widened<short> negative(-19, 65531);
	if (negative.signed_value != -19 || negative.unsigned_value != 65531 || negative.floating_value != -19.0) return 1;
	Widened<signed char> narrow(-7, 49157);
	if (narrow.signed_value != -7 || narrow.unsigned_value != 49157 || narrow.floating_value != -7.0) return 2;
	Widened<int> positive(23, 32769);
	if (positive.signed_value != 23 || positive.unsigned_value != 32769 || positive.floating_value != 23.0) return 3;
	Defaults defaults;
	return defaults.value == -19 ? 0 : 4;
}
