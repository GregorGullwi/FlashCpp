// Reduced regression for DiagnosticId::PointerToReferenceType (1001).
// Pointer-to-reference is ill-formed per C++20 [dcl.ref]/1; the declarator
// family diagnostic must reject it with a located structured error.
using R = int&;

int main() {
	int x = sizeof(R*);
	return x;
}
