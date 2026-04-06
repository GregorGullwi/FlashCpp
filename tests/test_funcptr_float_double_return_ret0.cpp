float half_it(int x) {
	return x / 2.0f;
}

double plus_quarter(int x) {
	return x + 0.25;
}

int checkf(float (*fp)(int), int x) {
	float f = fp(x);
	return (f > 4.99f && f < 5.01f) ? 0 : 1;
}

int checkd(double (*fp)(int), int x) {
	double d = fp(x);
	return (d > 7.24 && d < 7.26) ? 0 : 1;
}

int main() {
	return checkf(half_it, 10) + checkd(plus_quarter, 7);
}
