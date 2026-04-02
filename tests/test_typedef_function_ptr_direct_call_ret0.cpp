typedef int (*IntFn)(int);
using IntFnAlias = IntFn;

int add_one(int x) {
	return x + 1;
}

int call_typedef(IntFn fn, int x) {
	return fn(x);
}

int call_alias(IntFnAlias fn, int x) {
	return fn(x);
}

int main() {
	int via_typedef = call_typedef(add_one, 41);
	int via_alias = call_alias(add_one, 9);
	return (via_typedef == 42 && via_alias == 10) ? 0 : 1;
}
