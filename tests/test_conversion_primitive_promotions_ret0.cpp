// Test: primitive promotions and their overload-resolution ranking.
// Ensures buildConversionPlan keeps classifying promotions more strongly than conversions.

int pickIntegral(int value) {
	return value;
}

int pickIntegral(long long value) {
	return (int)(value + 100);
}

int pickFloating(double value) {
	return (int)(value * 2.0);
}

int pickFloating(long double value) {
	return (int)(value * 3.0L);
}

int main() {
	short s = 7;
	if (pickIntegral(s) != 7) return 1;

	bool flag = true;
	if (pickIntegral(flag) != 1) return 2;

	char c = 'A';
	if (pickIntegral(c) != 65) return 3;

	float f = 4.5f;
	if (pickFloating(f) != 9) return 4;

	return 0;
}
