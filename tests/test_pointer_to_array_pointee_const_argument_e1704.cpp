// Dropping pointee const is not a qualification conversion: the argument
// pointer must stay convertible to the parameter's pointer-to-array type at
// every level below the pointer object, so this call is not viable.
int read_row(int (*row)[3]) {
	return (*row)[1];
}

int main() {
	const int values[3] = {1, 2, 3};
	const int (*row)[3] = &values;
	return read_row(row);
}
