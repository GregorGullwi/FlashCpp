// PR boundary 11: SymbolTable insert rollback when DeclarationBuilder rejects
// publication after insert on the wired namespace/global free-function path.
// Return-type-only redeclarations are ill-formed, but SymbolTable may append an
// overload while the telemetry signature interner collapses parameter lists.

int symtab_insert_undo_row(int value);
float symtab_insert_undo_row(int value);

int main() {
	return 42;
}
