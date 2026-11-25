// [expr.ref]/2: E1->E2 is (*(E1)).E2, so the pointee's const qualification is
// the member object's qualification. An interleaved member reached through a
// pointer to const must be diagnosed exactly like the dot form.
struct S {
	int* (*p)[3];
};

S s;
const S* ps = &s;

int main() {
	ps->p = nullptr;
	return 0;
}
