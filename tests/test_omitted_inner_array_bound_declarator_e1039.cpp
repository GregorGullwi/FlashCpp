// [dcl.array]/1: named declarators reject a misplaced omitted inner bound as
// well, so int a[3][] cannot silently become an array of unknown bound of int.
int main() {
	int a[3][];

	return static_cast<int>(sizeof(a));
}
