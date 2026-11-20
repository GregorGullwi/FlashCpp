// [expr.ref]: an interleaved pointer member such as int* (*)[3] is a pointer
// object whose flat projection is empty. Member access must qualify and return
// the ordered pointer wrapper instead of reading a missing flat level; the
// previous projection read was undefined and crashed on this shape.
struct Rows {
	int* (*rows)[3];
};

int main() {
	int* values[3];
	int* other[3];
	Rows holder{&values};
	if (sizeof(holder.rows) != sizeof(void*)) return 1;
	auto& alias = holder.rows;
	if (alias != &values) return 2;
	alias = &other;
	if (holder.rows != &other) return 3;
	if (sizeof(*holder.rows) != 3 * sizeof(int*)) return 4;
	const Rows& view = holder;
	if (view.rows != &other) return 5;
	return 42;
}
