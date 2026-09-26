// Conditional common-type selection for pointers to arrays must preserve the
// nested element qualification when the branches add const to the pointee.
int select_row(const int (*row)[3]) {
	return (*row)[1];
}

int select_row(int (*row)[3]) {
	return -1;
}

int main() {
	int values[2][3] = {{1, 42, 3}, {4, 5, 6}};
	int (*mutable_rows)[3] = values;
	const int (*const_rows)[3] = values;
	return select_row((1 == 1) ? mutable_rows : const_rows);
}
