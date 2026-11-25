// [expr.ref]/2: the receiver's const qualification reaches the outermost
// ordered wrapper at any array depth. Two interleaved array levels must not
// hide the const pointer object inside the spine.
struct S {
	int* (*p)[2][3];
};

const S s;

int main() {
	auto& r = s.p;
	r = nullptr;
	return 0;
}
