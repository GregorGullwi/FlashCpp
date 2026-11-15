// Regression: a record-valued built-in subscript exposes the selected
// element's address, while a pointer-valued element stays a loaded pointer
// value. This covers nested member access, by-value aggregate returns, array
// decay, pointer-to-record array elements, and returns generated after main.

struct Small {
	short tag;
	int value;
};

struct Large {
	char tag;
	int values[5];
};

struct Holder {
	Small small;
	Large large;
};

struct Grid {
	Holder rows[2];
};

Holder late_holder;
Grid late_grid;

Small readSmall(Holder& holder) {
	return holder.small;
}

Large readLarge(Holder& holder) {
	return holder.large;
}

Small readGridSmall(Grid& grid) {
	return grid.rows[1].small;
}

Large readGridLarge(Grid& grid) {
	return grid.rows[1].large;
}

Small readLateMember();
Small readLateElement();
Large readLateLarge();

int main() {
	Grid grid;
	grid.rows[0].small.tag = 1;
	grid.rows[0].small.value = 10;
	grid.rows[0].large.tag = 2;
	grid.rows[0].large.values[0] = 20;
	grid.rows[0].large.values[1] = 21;
	grid.rows[0].large.values[2] = 22;
	grid.rows[0].large.values[3] = 23;
	grid.rows[0].large.values[4] = 24;
	grid.rows[1].small.tag = 3;
	grid.rows[1].small.value = 30;
	grid.rows[1].large.tag = 4;
	grid.rows[1].large.values[0] = 40;
	grid.rows[1].large.values[1] = 41;
	grid.rows[1].large.values[2] = 42;
	grid.rows[1].large.values[3] = 43;
	grid.rows[1].large.values[4] = 44;

	Small small = readSmall(grid.rows[1]);
	if (small.tag != 3 || small.value != 30) {
		return 1;
	}
	Large large = readLarge(grid.rows[1]);
	if (large.tag != 4 || large.values[0] != 40 || large.values[4] != 44) {
		return 2;
	}
	if (readGridSmall(grid).value != 30) {
		return 3;
	}
	Large nested = readGridLarge(grid);
	if (nested.tag != 4 || nested.values[4] != 44) {
		return 4;
	}

	Small* smalls[2] = {&grid.rows[0].small, &grid.rows[1].small};
	Small* first_small = smalls[0];
	Small* second_small = smalls[1];
	if (first_small->value != 10 || second_small->value != 30) {
		return 5;
	}

	Large* larges[2] = {&grid.rows[0].large, &grid.rows[1].large};
	Large* first_large = larges[0];
	Large* second_large = larges[1];
	if (first_large->values[4] != 24 || second_large->values[0] != 40) {
		return 6;
	}

	int* values = grid.rows[1].large.values;
	if (values[4] != 44) {
		return 7;
	}

	late_holder.small.tag = 5;
	late_holder.small.value = 50;
	late_grid.rows[1].small.tag = 6;
	late_grid.rows[1].small.value = 60;
	late_grid.rows[1].large.tag = 7;
	late_grid.rows[1].large.values[0] = 70;
	late_grid.rows[1].large.values[4] = 74;
	Small late_member = readLateMember();
	if (late_member.tag != 5 || late_member.value != 50) {
		return 8;
	}
	Small late_element = readLateElement();
	if (late_element.tag != 6 || late_element.value != 60) {
		return 9;
	}
	Large late_large = readLateLarge();
	if (late_large.tag != 7 || late_large.values[0] != 70 || late_large.values[4] != 74) {
		return 10;
	}

	return 42;
}

Small readLateMember() {
	return late_holder.small;
}

Small readLateElement() {
	return late_grid.rows[1].small;
}

Large readLateLarge() {
	return late_grid.rows[1].large;
}
