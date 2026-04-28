// Test that const vs non-const conversion operator preference is respected for
// built-in subscript expressions.
//
// DualConv has both a non-const and a const conversion operator. A non-const
// object must prefer the non-const operator (nc_data) over the const one
// (c_data). The two paths return pointers to different data, so the return value
// distinguishes which operator was selected: 42 means the non-const operator was
// used, 198 would indicate the const operator was used (incorrectly).

struct DualConv {
	int nc_data[2]; // [10, 32] — accessed via non-const operator int*()
	int c_data[2];  // [99, 99] — accessed via const operator const int*() const

	operator int*() { return &nc_data[0]; }
	operator const int*() const { return &c_data[0]; }
};

int main() {
	DualConv d;
	d.nc_data[0] = 10;
	d.nc_data[1] = 32;
	d.c_data[0] = 99;
	d.c_data[1] = 99;

	// Non-const object: must prefer non-const operator -> nc_data[0] + nc_data[1] = 42
	// Using the const operator would give c_data[0] + c_data[1] = 198 (wrong)
	return d[0] + d[1];
}
