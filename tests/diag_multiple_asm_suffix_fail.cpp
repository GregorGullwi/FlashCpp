// Reduced regression for DiagnosticId::MultipleAsmSuffixesOnDeclarator (1002).
// A declaration may carry at most one __asm label; a second one must be
// rejected instead of being silently accepted with last-one-wins semantics.
int x __asm("sym_a") __asm("sym_b");

int main() {
	return x;
}

// expected-diag: error MultipleAsmSuffixesOnDeclarator#1002 4:6
