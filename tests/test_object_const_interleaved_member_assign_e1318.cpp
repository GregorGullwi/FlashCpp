// [expr.ref]/2: a non-mutable member of a const object is const at its top
// level. An interleaved pointer/array member such as int* (*)[3] has no flat
// pointer projection, so the member's constness lives on the ordered pointer
// wrapper and must still reject assignment.
struct S {
	int* (*p)[3];
};

const S s;

int main() {
	auto& r = s.p;
	r = nullptr;
	return 0;
}
