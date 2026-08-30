// PR boundary 8: namespace-scope free functions publish through DeclarationBuilder
// after SymbolTable insert. Compatible redeclaration merging must keep compilation valid.

void decl_builder_wire_free_fn();
void decl_builder_wire_free_fn() {}

int main() {
	return 42;
}
