float half_it(int x) {
	return x / 2.0f;
}

double plus_quarter(int x) {
	return x + 0.25;
}

float callf(float (*fp)(int), int x) {
	return fp(x);
}

double calld(double (*fp)(int), int x) {
	return fp(x);
}

int main() {
	float f = callf(half_it, 10);
	double d = calld(plus_quarter, 7);
	return (f > 4.99f && f < 5.01f && d > 7.24 && d < 7.26) ? 0 : 1;
}
