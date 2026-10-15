// The `->*` form must also store through the member-pointer offset.

struct S {
	int x;
};

int main() {
	S s{0};
	int S::* pointer = &S::x;
	S* object = &s;
	object->*pointer = 42;
	return s.x;
}
