struct Plain {
	int value = 11;
};

int main() {
	Plain plain;
	Plain& plain_ref = plain;

	const void* expr_type = typeid(plain);
	const void* ref_expr_type = typeid(plain_ref);
	const void* exact_type = typeid(Plain);

	if (!expr_type || !ref_expr_type || !exact_type) {
		return 1;
	}
	if (expr_type != exact_type) {
		return 2;
	}
	if (ref_expr_type != exact_type) {
		return 3;
	}
	return 0;
}
