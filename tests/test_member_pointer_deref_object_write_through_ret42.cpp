// The explicit-dereference object form `(*pointer).*member` must also store
// through, equivalent to `pointer->*member`.

struct S {
	int x;
};

int main() {
	S s{0};
	int S::* member = &S::x;
	S* object = &s;
	(*object).*member = 42;
	return s.x;
}
