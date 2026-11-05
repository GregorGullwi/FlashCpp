// C++20 [dcl.spec.auto]/[conv.array]: value `auto` decays an array initializer
// to a pointer to its first element, while `auto&` binds the array itself and
// `auto*` from `&array` is a pointer to the whole array. An interleaved
// declarator such as int (*[N])[M] has no flat projection, so deduction keeps
// the ordered spine instead of the flat base element type.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};
int (*cells[2])[4] = {&storage, &other};

struct Holder {
	int (*cells[2])[4];
};

int main() {
	auto values = cells;
	if (sizeof(values) != sizeof(void*)) return 1;
	if ((*values[0])[1] != 8) return 2;
	if ((*values[1])[2] != 3) return 3;

	auto& ref = cells;
	if (sizeof(ref) != sizeof(cells)) return 4;
	if ((*ref[0])[3] != 10) return 5;

	auto* pointer = &cells;
	if (sizeof(*pointer) != sizeof(cells)) return 6;
	if ((*pointer)[1][0][0] != 1) return 7;

	Holder holder{{&storage, &other}};
	auto member_values = holder.cells;
	if ((*member_values[0])[3] != 10) return 8;
	auto& member_ref = holder.cells;
	if ((*member_ref[1])[1] != 2) return 9;
	return 42;
}
