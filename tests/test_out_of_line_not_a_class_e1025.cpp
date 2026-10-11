// An out-of-line member definition whose owner resolves to a non-class type is
// reported as OutOfLineDefinitionNotAClass (1025).
using Alias = int;

int Alias::f() {
	return 0;
}

int main() {
	return 0;
}
