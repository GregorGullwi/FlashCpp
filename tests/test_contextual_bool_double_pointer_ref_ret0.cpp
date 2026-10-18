// Contextual conversion of a floating-point pointer reference to bool must test
// the pointer value. A null double* or float* passed by reference is false, and
// a non-null pointer is true, including when the pointee bits are zero.
int nonnull_double(double*& value) { return value ? 1 : 0; }
int nonnull_float(float*& value) { return value ? 1 : 0; }

int main() {
	double number = 0.0;
	double* present = &number;
	double* absent = nullptr;
	if (nonnull_double(present) != 1)
		return 1;
	if (nonnull_double(absent) != 0)
		return 2;

	float small = 0.0f;
	float* present_float = &small;
	float* absent_float = nullptr;
	if (nonnull_float(present_float) != 1)
		return 3;
	if (nonnull_float(absent_float) != 0)
		return 4;

	double*& local_present = present;
	double*& local_absent = absent;
	if ((local_present ? 1 : 0) != 1)
		return 5;
	if ((local_absent ? 1 : 0) != 0)
		return 6;

	return 0;
}
