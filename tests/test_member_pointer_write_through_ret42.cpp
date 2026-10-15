// Assigning through a member-object pointer must store into the object, not a
// temporary. `s.*p` is an lvalue, so the LValueAddress lowering must return the
// member address and mark it indirect.

struct S {
	int x;
};

int main() {
	S s{0};
	int S::* pointer = &S::x;
	s.*pointer = 42;
	return s.x;
}
