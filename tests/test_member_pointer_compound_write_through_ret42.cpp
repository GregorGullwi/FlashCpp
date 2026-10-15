// Compound assignment through a member-object pointer must read-modify-store
// through the member address, not a loaded temporary.

struct S {
	int x;
};

int main() {
	S s{0};
	int S::* pointer = &S::x;
	s.*pointer += 42;
	return s.x;
}
