// [dcl.array]/1: only the outermost array bound may be omitted. A direct
// type-id with a misplaced inner omitted bound is diagnosed at the bracket, not
// later as an unknown-bound sizeof.
int main() {
	return static_cast<int>(sizeof(int[3][]));
}
