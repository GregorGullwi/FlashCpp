// An out-of-line static data-member definition whose type conflicts with the
// in-class declaration is invalid and must report OutOfLineDefinitionMismatch
// (1022) instead of silently replacing the member's type or storage.

struct S {
	static int data;
};

double S::data;

int main() {
	return 0;
}
