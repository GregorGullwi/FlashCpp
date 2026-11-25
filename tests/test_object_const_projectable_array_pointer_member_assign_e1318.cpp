// [expr.ref]/2: the projectable pointer-to-array member shape has always been
// diagnosed. Keep it on the same diagnostic as the interleaved shape so the
// flat and ordered const-object paths cannot drift apart.
struct S {
	int (*array_pointer)[3];
};

const S s;

int main() {
	auto& array_pointer = s.array_pointer;
	array_pointer = nullptr;
	return 0;
}
