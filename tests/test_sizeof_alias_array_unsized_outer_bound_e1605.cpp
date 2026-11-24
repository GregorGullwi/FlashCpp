// [dcl.array]/1, [expr.sizeof]/2: an omitted declarator bound around an array
// alias is a new unknown outer bound. sizeof(a) for A a[] with A=int[3] must
// reject instead of reporting the alias target's int[3] width.
using Row = int[3];

int main() {
	Row a[];

	return static_cast<int>(sizeof(a));
}
