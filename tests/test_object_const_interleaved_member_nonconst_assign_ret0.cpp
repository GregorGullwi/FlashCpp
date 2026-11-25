// [expr.ref]/2: only the receiver's constness qualifies a member object. Both
// plain assignment on a non-const object and assignment through const_cast must
// write the member itself; every store is read back through the const view so a
// dropped store or a wrong address cannot pass. The long long row and the 2D
// planes keep a pointer/stride mistake visible as a wrong value, and each shape
// lives in its own type so a failure points at one member.
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

int first = 5;
int second = 6;
int third = 7;
int fourth = 15;
int fifth = 16;
int sixth = 17;
long long wide_first = 1LL << 34;
long long wide_second = (1LL << 41) + 3;
long long wide_third = (1LL << 45) + 9;
long long wide_fourth = (1LL << 47) + 11;

int* first_row[3] = {&first, &second, &third};
int* second_row[3] = {&fourth, &fifth, &sixth};
long long* first_wide[2] = {&wide_first, &wide_second};
long long* second_wide[2] = {&wide_third, &wide_fourth};
int first_plane[2][3] = {{5, 6, 7}, {15, 16, 17}};
int second_plane[2][3] = {{17, 16, 15}, {7, 6, 5}};
long long first_wide_plane[2][2] = {{1LL << 34, (1LL << 36) + 3}, {(1LL << 45) + 9, (1LL << 47) + 11}};
long long second_wide_plane[2][2] = {{(1LL << 47) + 11, (1LL << 45) + 9}, {(1LL << 36) + 3, 1LL << 34}};

int main() {
	Ints ints_holder{&first_row};
	const Ints& ints_view = ints_holder;
	auto& rewritten_ints = const_cast<Ints&>(ints_view).p;
	rewritten_ints = &second_row;
	if (*(*ints_view.p)[0] != 15) return 1;
	if (*(*ints_view.p)[2] != 17) return 2;
	ints_holder.p = &first_row;
	if (*(*ints_holder.p)[2] != 7) return 3;

	Wide wide_holder{&first_wide};
	const Wide& wide_view = wide_holder;
	auto& rewritten_wide = const_cast<Wide&>(wide_view).p;
	rewritten_wide = &second_wide;
	if (*(*wide_view.p)[0] != wide_third) return 4;
	if (*(*wide_view.p)[1] != wide_fourth) return 5;

	Plane plane_holder{&first_plane};
	const Plane& plane_view = plane_holder;
	auto& rewritten_plane = const_cast<Plane&>(plane_view).p;
	rewritten_plane = &second_plane;
	if ((*plane_view.p)[0][0] != 17) return 6;
	if ((*plane_view.p)[1][2] != 5) return 7;
	plane_holder.p = &first_plane;
	if ((*plane_holder.p)[1][0] != 15) return 8;

	WidePlane wide_plane_holder{&first_wide_plane};
	const WidePlane& wide_plane_view = wide_plane_holder;
	auto& rewritten_wide_plane = const_cast<WidePlane&>(wide_plane_view).p;
	rewritten_wide_plane = &second_wide_plane;
	if ((*wide_plane_view.p)[0][1] != (1LL << 45) + 9) return 9;
	if ((*wide_plane_view.p)[1][0] != (1LL << 36) + 3) return 10;

	if (wide_plane_holder.p != &second_wide_plane || ints_holder.p != &first_row) return 11;
	return 0;
}
