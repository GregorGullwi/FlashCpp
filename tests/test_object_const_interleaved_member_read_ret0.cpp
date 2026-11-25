// [expr.ref]/2: reading through a const object's interleaved member must
// resolve the ordered pointer wrapper and then the arrays it points at. The
// distinct int and long long rows make a pointer-sized-instead-of-element-sized
// stride read a wrong address. Each shape lives in its own type so a failure
// points at one member layout, and the two-dimensional members check both outer
// and inner extents.
struct Ints {
	int* (*p)[3];
};

struct Wide {
	long long* (*p)[2];
};

struct Plane {
	int (*p)[2][3];
};

struct WidePlane {
	long long (*p)[2][2];
};

int low = 11;
int mid = 22;
int high = 33;
long long huge_low = 1LL << 33;
long long huge_high = (1LL << 40) + 7;

int* ints_row[3] = {&low, &mid, &high};
long long* wide_row[2] = {&huge_low, &huge_high};
int plane_cells[2][3] = {{11, 22, 33}, {44, 55, 66}};
long long wide_plane_cells[2][2] = {{1LL << 33, (1LL << 35) + 1}, {(1LL << 45) + 13, 1LL << 47}};

int main() {
	const Ints ints_holder{&ints_row};
	auto& ints = ints_holder.p;
	if (ints != &ints_row) return 1;
	if (*(*ints)[0] != 11) return 2;
	if (*(*ints)[2] != 33) return 3;
	if (sizeof(*ints_holder.p) != 3 * sizeof(int*)) return 4;

	const Wide wide_holder{&wide_row};
	if (*(*wide_holder.p)[0] != huge_low) return 5;
	if (*(*wide_holder.p)[1] != huge_high) return 6;
	if (sizeof(*wide_holder.p) != 2 * sizeof(long long*)) return 7;

	const Plane plane_holder{&plane_cells};
	if ((*plane_holder.p)[0][2] != 33) return 8;
	if ((*plane_holder.p)[1][0] != 44) return 9;
	if ((*plane_holder.p)[1][2] != 66) return 10;
	if (sizeof(*plane_holder.p) != 2 * 3 * sizeof(int)) return 11;

	const WidePlane wide_plane_holder{&wide_plane_cells};
	if ((*wide_plane_holder.p)[0][1] != (1LL << 35) + 1) return 12;
	if ((*wide_plane_holder.p)[1][0] != (1LL << 45) + 13) return 13;
	if ((*wide_plane_holder.p)[1][1] != 1LL << 47) return 14;
	if (sizeof(*wide_plane_holder.p) != 2 * 2 * sizeof(long long)) return 15;

	return 0;
}
