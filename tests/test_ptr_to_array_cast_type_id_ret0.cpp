// Regression: cast type-ids are type-ids (C++20 [expr.static.cast]/1,
// [expr.reinterpret.cast]/1, [expr.cast]/1, [dcl.name]/1), so they may contain a
// parenthesized abstract-declarator group. static_cast<int(*)[3]>(v),
// reinterpret_cast<long(*)[2][4]>(v) and the C-style (int(*)[3])v used to be
// rejected by the parser ("Expected '>' after type in static_cast" / "Expected
// primary expression") because the cast paths only consumed trailing cv- and
// ptr-operators. The group must bind the array suffix inside the pointer, giving
// the same pointee shape as the named spelling int (*p)[3].

int ints[2][3] = {{1, 2, 3}, {4, 5, 6}};
long longs[2][4] = {{10, 11, 12, 13}, {14, 15, 16, 17}};

struct Cell {
	int value;
	long stamp;
};

Cell cells[2][2] = {{{1, 100}, {2, 200}}, {{3, 300}, {4, 400}}};

template <typename T>
T through_reinterpret(void* raw, int row, int col) {
	T (*view)[2][4] = reinterpret_cast<T(*)[2][4]>(raw);
	return (*view)[row][col];
}

int static_cast_path() {
	void* raw = static_cast<void*>(ints);
	int (*view)[3] = static_cast<int(*)[3]>(raw);
	// (*view)[i] walks the first row of the pointee array[3].
	int first = (*view)[0];
	int second = (*view)[1];
	int third = (*view)[2];
	return (first == 1 && second == 2 && third == 3) ? 0 : 1;
}

int c_style_path() {
	void* raw = (void*)ints;
	int (*view)[3] = (int(*)[3])raw;
	int first = (*view)[2];
	return first == 3 ? 0 : 2;
}

int const_pointee_path() {
	void* raw = static_cast<void*>(ints);
	const int (*view)[3] = static_cast<const int(*)[3]>(raw);
	return (*view)[1] == 2 ? 0 : 4;
}

int reinterpret_multibound_path() {
	void* raw = reinterpret_cast<void*>(longs);
	long (*view)[2][4] = reinterpret_cast<long(*)[2][4]>(raw);
	long low = (*view)[0][0];
	long high = (*view)[1][3];
	return (low == 10 && high == 17) ? 0 : 8;
}

int template_path() {
	void* raw = static_cast<void*>(longs);
	return through_reinterpret<long>(raw, 1, 1) == 15 ? 0 : 16;
}

int struct_element_path() {
	void* raw = static_cast<void*>(cells);
	Cell (*view)[2] = static_cast<Cell(*)[2]>(raw);
	int value = (*view)[1].value;
	long stamp = (*view)[0].stamp;
	return (value == 2 && stamp == 100) ? 0 : 32;
}

// A parenthesized expression that merely looks like a cast must stay an
// expression: (mask) & value is a bitwise and, not a cast to a reference type.
int not_a_cast_path() {
	int mask = 6;
	int value = 3;
	int combined = (mask) & value;
	return combined == 2 ? 0 : 64;
}

int main() {
	return static_cast_path() + c_style_path() + const_pointee_path() +
		   reinterpret_multibound_path() + template_path() +
		   struct_element_path() + not_a_cast_path();
}
