// Test: invalid integer literal suffix must be rejected.
// C++20 [lex.icon] only allows: u, l, ul, lu, ll, ull, llu (case-insensitive).
// "LLL" is not a valid suffix and should produce a compile error.

int main() {
	int x = 0LLL;
	return x;
}
