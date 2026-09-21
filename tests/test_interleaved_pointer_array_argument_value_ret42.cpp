// Ordered pointer-to-array objects lower as pointer-sized values: global and
// local identifier loads, by-value argument transfer, and ordered pointer
// parameter comparison all consume the ordered declarator spine.
int (*(*int_pointer)[3])[4] = nullptr;
double (*(*double_pointer)[2])[3] = nullptr;

int int_pointer_is_null(int (*(*param)[3])[4]) {
	return param == nullptr ? 1 : 0;
}

int double_pointer_is_null(double (*(*param)[2])[3]) {
	return param == nullptr ? 1 : 0;
}

int main() {
	int (*(*local_int_pointer)[3])[4] = int_pointer;
	double (*(*local_double_pointer)[2])[3] = double_pointer;
	if (int_pointer_is_null(int_pointer) != 1) {
		return 1;
	}
	if (int_pointer_is_null(local_int_pointer) != 1) {
		return 2;
	}
	if (double_pointer_is_null(double_pointer) != 1) {
		return 3;
	}
	if (double_pointer_is_null(local_double_pointer) != 1) {
		return 4;
	}
	return 42;
}
