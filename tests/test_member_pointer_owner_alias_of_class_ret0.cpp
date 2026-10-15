// A member-object pointer whose owner is an alias of a class is valid: the alias
// resolves to the class and must not be rejected as a non-class owner. This is a
// compile-only guard against over-rejection at the owner-binding step.

struct S {
	int x;
};

using Alias = S;

int Alias::* aliased_pointer = &S::x;
int S::* plain_pointer = &S::x;

void observe(S& value) {
	(void)value;
	(void)aliased_pointer;
	(void)plain_pointer;
}
