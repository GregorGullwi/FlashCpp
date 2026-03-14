// Test: implicit float→int and double→int conversion in return statements (Phase 2 sema migration).
// The semantic pass annotates the return expression; AstToIr reads the annotation.
// Note: literal float returns trigger a pre-existing codegen limitation (handleFloatToInt
// expects a register, not a double immediate). Use variables here to exercise the conversion.

int float_var_to_int() {
	float f = 5.0f;
	return f;
}

int double_var_to_int() {
	double d = 9.0;
	return d;
}

int main() {
	int a = float_var_to_int();
	int b = double_var_to_int();
	return (a - 5) + (b - 9);
}
