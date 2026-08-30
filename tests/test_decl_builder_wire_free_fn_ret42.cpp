// PR boundary 8: global and namespace free functions publish through the
// DeclarationBuilder shadow path after SymbolTable insert.

void decl_builder_wire_global_fn();
void decl_builder_wire_global_fn() {}

namespace decl_builder_wire_ns {
void wire_ns_fn();
void wire_ns_fn() {}
}

int main() {
	return 42;
}
