int read_first(int* const& pointer) {
	return *pointer;
}

int read_first(short* const& pointer) {
	return *pointer;
}

int main() {
	int values[2] = {41, 7};
	short small_values[1] = {1};
	return read_first(values) + read_first(small_values);
}
