// An array cannot bind to a reference to a scalar builtin. Only a reference to
// the array type itself is a valid reference binding.
static int consume(int& value) {
	return value;
}

int main() {
	int values[3] = {1, 2, 3};
	return consume(values);
}
