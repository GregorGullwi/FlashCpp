struct DualConv {
	int nc_data[2];
	int c_data[2];
	operator int*() { return &nc_data[0]; }
	operator const int*() const { return &c_data[0]; }
};

int main() {
	DualConv d;
	d.nc_data[0] = 10;
	d.nc_data[1] = 32;
	d.c_data[0] = 99;
	d.c_data[1] = 99;

	const DualConv& cd = d;
	return d[0] + d[1] + (cd[0] + cd[1] - 198);
}
