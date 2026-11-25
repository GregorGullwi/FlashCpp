// [expr.ref]/2: a reference member is not itself cv-qualified by the object
// ([dcl.ref]/1), so assigning through a const object's reference-to-interleaved
// wrapper assigns the referent. The object's constness must not leak onto the
// reference's ordered spine, and the stored pointer must be visible through the
// separate storage path.
struct Store {
	int* (*&rows)[3];
};

int first = 1;
int second = 2;
int third = 3;
int fourth = 4;
int fifth = 5;
int sixth = 6;

int* first_row[3] = {&first, &second, &third};
int* second_row[3] = {&fourth, &fifth, &sixth};

int main() {
	int* (*storage)[3] = &first_row;
	Store store{storage};
	const Store& view = store;

	auto& rows = view.rows;
	if (rows != &first_row) return 1;
	if (*(*rows)[1] != 2) return 2;

	rows = &second_row;
	if (storage != &second_row) return 3;
	if (*(*view.rows)[0] != 4) return 4;
	if (*(*store.rows)[2] != 6) return 5;

	return 0;
}
