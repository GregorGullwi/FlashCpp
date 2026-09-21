// A namespace-scope qualified identifier must contribute its declared ordered
// type to overload resolution and lower as a pointer-sized value, exactly like
// the unqualified form. Covers nested namespaces, multiple base types, pointer
// cv, a local copy, by-value transfer, and comparison.
namespace outer {
namespace inner {
int (*(*int_value)[3])[4] = nullptr;
double (*(*double_value)[2])[3] = nullptr;
char (* const (*pointer_cv_value)[3])[4] = nullptr;
}
}

int consumes_int(int (*(*)[3])[4]) { return 7; }
int consumes_double(double (*(*)[2])[3]) { return 9; }
int consumes_pointer_cv(char (* const (*)[3])[4]) { return 11; }

int main() {
	int (*(*local_int)[3])[4] = outer::inner::int_value;
	double (*(*local_double)[2])[3] = outer::inner::double_value;
	if (consumes_int(outer::inner::int_value) != 7) {
		return 1;
	}
	if (consumes_int(local_int) != 7) {
		return 2;
	}
	if (consumes_double(outer::inner::double_value) != 9) {
		return 3;
	}
	if (consumes_double(local_double) != 9) {
		return 4;
	}
	if (consumes_pointer_cv(outer::inner::pointer_cv_value) != 11) {
		return 5;
	}
	if (outer::inner::int_value != nullptr) {
		return 6;
	}
	if (outer::inner::pointer_cv_value != nullptr) {
		return 7;
	}
	return 42;
}
