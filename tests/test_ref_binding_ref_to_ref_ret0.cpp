// Regression test: reference-to-reference binding (const T& r2 = r1 where r1 is T&)
// Bug: addr_temp from LValueAddress eval of r1 had pointee size_in_bits (32), so the old
// is_likely_pointer check (size==64) failed → LEA was emitted instead of MOV, giving r2
// the address of r1's stack slot rather than the address of i.
// Also covers: nested scopes, for/while loops, double, array element ref, and struct types.

struct Point {
	int x;
	int y;
};

int sum_through_ref(const Point& p) {
	return p.x + p.y;
}

int main() {
 // ---- int: basic ref-to-ref aliasing ----
	int i = 42;
	int& r1 = i;

	const int& r2 = r1;
	if (r2 != 42)
		return 2;

	i = 99;
	if (r1 != 99)
		return 4;
	if (r2 != 99)
		return 5;

	r1 = 7;
	if (r2 != 7)
		return 6;
	if (i != 7)
		return 7;

 // ---- double: ref-to-ref ----
	double d = 1.5;
	double& dr1 = d;
	const double& dr2 = dr1;
	if (dr2 != 1.5)
		return 8;
	d = 2.5;
	if (dr1 != 2.5)
		return 9;
	if (dr2 != 2.5)
		return 10;

 // ---- struct: ref-to-ref ----
	Point p{3, 4};
	Point& pr1 = p;
	const Point& pr2 = pr1;
	if (pr2.x != 3 || pr2.y != 4)
		return 11;
	p.x = 10;
	if (pr1.x != 10)
		return 12;
	if (pr2.x != 10)
		return 13;
	if (sum_through_ref(pr2) != 14)
		return 14;  // 10+4

 // ---- nested code block: ref-to-ref ----
	{
		int a = 20;
		int& ra = a;
		const int& ra2 = ra;
		if (ra2 != 20)
			return 15;
		a = 30;
		if (ra2 != 30)
			return 16;
	}

 // ---- for loop: ref-to-ref on array elements ----
	int arr[3] = {1, 2, 3};
	int sum = 0;
	for (int idx = 0; idx < 3; ++idx) {
		int& elem_ref = arr[idx];
		const int& elem_ref2 = elem_ref;
		sum += elem_ref2;
		arr[idx] += 10;
		if (elem_ref2 != arr[idx])
			return 17;  // ref2 must see the update
	}
	if (sum != 6)
		return 18;  // 1+2+3 before updates

 // ---- while loop: ref-to-ref on a local variable ----
	int w = 0;
	int counter = 3;
	while (counter > 0) {
		int& wr = counter;
		const int& wr2 = wr;
		w += wr2;
		--counter;
		if (wr2 != counter)
			return 19;  // ref2 must see the decrement
	}
	if (w != 6)
		return 20;  // 3+2+1

	return 0;
}
