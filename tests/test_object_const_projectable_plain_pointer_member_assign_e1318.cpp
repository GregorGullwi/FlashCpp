// [expr.ref]/2: the plain pointer member shape has always been diagnosed. Keep
// it on the same diagnostic as the interleaved shape so the flat and ordered
// const-object paths cannot drift apart.
struct S {
	int* plain_pointer;
};

const S s;

int main() {
	auto& plain_pointer = s.plain_pointer;
	plain_pointer = nullptr;
	return 0;
}
