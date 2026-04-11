// Test: scalar heap initialization preserves float immediates for both value-init
// and direct-init forms.

int main() {
	float* f0 = new float{};
	if (*f0 != 0.0f) {
		delete f0;
		return 1;
	}
	delete f0;

	float* f1 = new float();
	if (*f1 != 0.0f) {
		delete f1;
		return 2;
	}
	delete f1;

	double* d0 = new double{};
	if (*d0 != 0.0) {
		delete d0;
		return 3;
	}
	delete d0;

	float* f2 = new float{1.5f};
	if (*f2 != 1.5f) {
		delete f2;
		return 4;
	}
	delete f2;

	float* f3 = new float(-2.5f);
	if (*f3 != -2.5f) {
		delete f3;
		return 5;
	}
	delete f3;

	float* f4 = new float{};
	*f4 = 1.25f;
	if (*f4 != 1.25f) {
		delete f4;
		return 6;
	}
	delete f4;

	double* d1 = new double{3.14};
	if (*d1 != 3.14) {
		delete d1;
		return 7;
	}
	delete d1;

	return 0;
}
