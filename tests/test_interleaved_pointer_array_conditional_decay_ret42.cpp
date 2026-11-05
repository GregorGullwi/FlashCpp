// C++20 [expr.cond]: an array operand of a conditional expression undergoes
// array-to-pointer conversion when the branches need a common type, and value
// auto deduction applies [conv.array] to the resulting array lvalue. An
// interleaved array such as int (*[N])[M] must decay to a pointer to its
// pointer-to-array element, not to the whole array object.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};
int (*cells[2])[4] = {&storage, &other};
int (*more[3])[4] = {&other, &storage, &other};

int main() {
	auto first = true ? cells : cells;
	if ((*first[0])[0] != 7) return 1;
	if ((*first[1])[2] != 3) return 2;

	int (**explicit_pointer)[4] = false ? cells : cells;
	if ((*explicit_pointer[1])[0] != 1) return 3;

	auto common = true ? cells : more;
	if ((*common[1])[3] != 4) return 4;

	auto tail = false ? cells : more;
	if ((*tail[2])[1] != 2) return 5;
	return 42;
}
