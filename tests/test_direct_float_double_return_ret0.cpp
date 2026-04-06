float half_it(int x) {
	return x / 2.0f;
}

double plus_quarter(int x) {
	return x + 0.25;
}

float callf(int x) {
	return half_it(x);
}

double calld(int x) {
	return plus_quarter(x);
}

int main() {
	float f = callf(10);
	double d = calld(7);
	return (f > 4.99f && f < 5.01f && d > 7.24 && d < 7.26) ? 0 : 1;
}
