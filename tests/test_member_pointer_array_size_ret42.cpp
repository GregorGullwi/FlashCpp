// An array of data-member pointers multiplies the member-pointer element size
// by its extents. The member pointer stays a scalar element, so its array
// bounds must stay outside the member-pointer shape instead of becoming its
// pointee.

struct C {
	int x;
};

struct Holder {
	int C::*cells[2];
	int marker;
};

int main() {
	int C::*single;
	int C::*cells[2];
	int C::*multi[2][3];
	if (sizeof(single) != 8) {
		return 1;
	}
	if (sizeof(cells) != 16) {
		return 2;
	}
	if (sizeof(multi) != 48) {
		return 3;
	}
	if (sizeof(multi[0]) != 24) {
		return 4;
	}
	if (sizeof(Holder) != 24) {
		return 5;
	}

	Holder holder{};
	holder.marker = 7;
	if (holder.marker != 7) {
		return 6;
	}

	int C::*copy[2] = {&C::x, &C::x};
	if (sizeof(copy) != 16) {
		return 7;
	}
	C object{5};
	if (object.*copy[1] != 5) {
		return 8;
	}
	return 42;
}
