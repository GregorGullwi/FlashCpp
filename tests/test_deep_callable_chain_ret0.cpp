// Each result retains the next callable's signature through semantic queries
// and lowering. Exercise the chain with the normal executable stack setting.
int fn0(int value) { return value + 2; }

int (*fn1(int))(int) { return fn0; }
int (*(*fn2(int))(int))(int) { return fn1; }
int (*(*(*fn3(int))(int))(int))(int) { return fn2; }
int (*(*(*(*fn4(int))(int))(int))(int))(int) { return fn3; }
int (*(*(*(*(*fn5(int))(int))(int))(int))(int))(int) { return fn4; }
int (*(*(*(*(*(*fn6(int))(int))(int))(int))(int))(int))(int) { return fn5; }
int (*(*(*(*(*(*(*fn7(int))(int))(int))(int))(int))(int))(int))(int) { return fn6; }
int (*(*(*(*(*(*(*(*fn8(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn7; }
int (*(*(*(*(*(*(*(*(*fn9(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn8; }
int (*(*(*(*(*(*(*(*(*(*fn10(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn9; }
int (*(*(*(*(*(*(*(*(*(*(*fn11(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn10; }
int (*(*(*(*(*(*(*(*(*(*(*(*fn12(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn11; }
int (*(*(*(*(*(*(*(*(*(*(*(*(*fn13(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn12; }
int (*(*(*(*(*(*(*(*(*(*(*(*(*(*fn14(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn13; }
int (*(*(*(*(*(*(*(*(*(*(*(*(*(*(*fn15(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) { return fn14; }
int (*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*fn16(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int))(int) {
	return fn15;
}

int main() {
	return fn16(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(0)(40) == 42 ? 0 : 1;
}
