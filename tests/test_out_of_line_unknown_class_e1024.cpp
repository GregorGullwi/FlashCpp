// An out-of-line member definition naming a class that was never declared is
// reported as OutOfLineDefinitionUnknownClass (1024).
struct S {};

int Unknown::f() {
	return 0;
}

int main() {
	return 0;
}
