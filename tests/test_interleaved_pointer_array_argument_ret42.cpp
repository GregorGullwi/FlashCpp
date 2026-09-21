// Overload/conversion resolution must consume the ordered declarator spine for
// non-projectable interleaved pointer/array shapes instead of rejecting them.
// The call operands stay inside sizeof so codegen never lowers an ordered
// pointer value; this isolates the conversion choke point.
int (*(*int_shape)[3])[4] = nullptr;
double (*(*double_shape)[2])[3][5] = nullptr;
char (* const (*pointer_cv_shape)[3])[4] = nullptr;

int accepts_int(int (*(*)[3])[4]) { return 1; }
int accepts_double(double (*(*)[2])[3][5]) { return 2; }
int accepts_pointer_cv(char (* const (*)[3])[4]) { return 3; }

int main() {
	const bool int_ok =
		sizeof(accepts_int(int_shape)) == sizeof(int);
	const bool double_ok =
		sizeof(accepts_double(double_shape)) == sizeof(int);
	const bool cv_ok =
		sizeof(accepts_pointer_cv(pointer_cv_shape)) == sizeof(int);
	return int_ok && double_ok && cv_ok ? 42 : 1;
}
