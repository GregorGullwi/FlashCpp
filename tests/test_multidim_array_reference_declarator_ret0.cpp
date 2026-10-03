// A reference declarator may carry a multi-dimensional array bound, such as
// `int (&rm)[2][2] = m;`. The parser must consume every extent after the
// parenthesized reference (not just the first), and the subscript chain must
// index through the referenced array's stored address rather than treating the
// reference's frame slot as inline element storage.
int sum2(const int (&r)[2][2]) {
	return r[0][0] + r[1][1];
}

int main() {
	int m[2][2] = {{1, 2}, {3, 4}};
	int (&rm)[2][2] = m;
	if (rm[1][1] != 4) return 1;
	if (sum2(m) != 5) return 2;

	rm[1][1] = 40;
	if (m[1][1] != 40) return 3;
	if (sum2(m) != 41) return 4;

	int t[2][2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
	int (&rt)[2][2][2] = t;
	if (rt[1][1][1] != 8) return 5;

	rt[0][0][0] = 9;
	if (t[0][0][0] != 9) return 6;
	return 0;
}
