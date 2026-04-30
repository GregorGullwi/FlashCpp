// Regression test: returned pointer arithmetic must preserve pointer depth, and
// local string-literal-initialized arrays must infer/copy their full contents.

const char* pick(const char* p, bool skip) {
	if (!skip)
		return p;
	return p + 1;
}

int main() {
	const char unsized[] = "*hidden";
	const char sized[8] = "*hidden";
	const char* r1 = pick(unsized, true);
	const char* r2 = pick(sized, true);
	if (r1[0] != 'h')
		return 1;
	if (r2[0] != 'h')
		return 2;
	return 0;
}
