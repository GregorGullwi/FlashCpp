// A second out-of-line definition of the same member function is reported as
// OutOfLineDefinitionAlreadyDefined (1027).
struct S {
	int f();
};

int S::f() {
	return 0;
}

int S::f() {
	return 1;
}

int main() {
	return 0;
}
