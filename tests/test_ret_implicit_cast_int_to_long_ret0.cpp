// Test: implicit int→long conversion in return statements (Phase 2 sema migration).
// The semantic pass annotates the return expression; AstToIr reads the annotation.

long from_literal() { return 42; }

long from_var() {
	int x = 100;
	return x;
}

long from_param(int p) { return p; }

int main() {
	long a = from_literal();
	long b = from_var();
	long c = from_param(7);
	return (int)(a - 42L) + (int)(b - 100L) + (int)(c - 7L);
}
