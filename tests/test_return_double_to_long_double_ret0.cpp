// Regression: returning a double expression from a long-double function
// must receive a sema-owned return conversion annotation.
long double from_double_literal() {
	return 1.0; // double -> long double
}

long double from_double_var(double value) {
	return value; // double -> long double
}

int main() {
	volatile long double a = from_double_literal();
	volatile long double b = from_double_var(2.0);
	(void)a;
	(void)b;
	return 0;
}
