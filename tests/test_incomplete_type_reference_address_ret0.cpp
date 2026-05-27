// Regression: reference identifiers to forward-declared types must not hard-fail
// during codegen when only their address is taken.
struct ForwardDeclared;

void touch(ForwardDeclared& value) {
	(void)&value;
}

struct ForwardDeclared {
	int x;
};

int main() {
	ForwardDeclared v{42};
	touch(v);
	return 0;
}
