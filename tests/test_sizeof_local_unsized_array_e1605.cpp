// [dcl.array]/1, [expr.sizeof]/2: a local array with an omitted outer bound has
// no object size. sizeof(a) must reject the declaration's unknown-bound array
// instead of falling back to the element size.
int main() {
	int a[];

	return static_cast<int>(sizeof(a));
}
