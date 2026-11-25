// [expr.const.cast]/4: const_cast removes the receiver's constness, and the
// resulting member object is again a modifiable lvalue. The interleaved spine
// must follow the cast rather than keep the wrapper's object const, and the
// written pointer must be visible through the original const reference.
struct S {
	int* (*p)[3];
};

int first = 21;
int second = 22;
int third = 23;
int fourth = 24;
int fifth = 25;
int sixth = 26;

int* row[3] = {&first, &second, &third};
int* other[3] = {&fourth, &fifth, &sixth};

int main() {
	const S s{&row};

	auto& r = const_cast<S&>(s).p;
	if (r != &row) return 1;
	r = &other;
	if (s.p != &other) return 2;
	if (*(*s.p)[0] != 24) return 3;
	if (*(*s.p)[2] != 26) return 4;

	const_cast<S&>(s).p = &row;
	if (s.p != &row) return 5;
	if (*(*s.p)[1] != 22) return 6;

	return 0;
}
