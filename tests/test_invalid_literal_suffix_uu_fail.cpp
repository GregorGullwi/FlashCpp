// Test: invalid integer literal suffix must be rejected.
// C++20 [lex.icon] only allows: u, l, ul, lu, ll, ull, llu (case-insensitive).
// "UU" is not a valid suffix and should produce a compile error.

int main() {
	int x = 0UU;
	return x;
}
