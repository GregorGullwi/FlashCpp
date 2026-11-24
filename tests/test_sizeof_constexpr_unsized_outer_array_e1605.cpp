// [dcl.array]/1, [expr.sizeof]/2: constant evaluation of sizeof(g) must reject
// an array of unknown bound instead of folding the inner long long[3] extent.
long long g[][3];

constexpr int value = sizeof(g);

int main() {
	return value;
}
