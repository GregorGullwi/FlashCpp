// [dcl.array]/1, [expr.sizeof]/2: long long[][3] is an array of unknown bound
// whose element type is long long[3]. sizeof(g) must reject the unknown outer
// bound instead of reporting the inner 24-byte extent.
long long g[][3];

int main() {
	return static_cast<int>(sizeof(g));
}
