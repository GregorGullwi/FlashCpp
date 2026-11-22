// An array object has no implicit conversion to a scalar builtin; only
// array-to-pointer decay and pointer-like-to-bool are standard conversions.
static int consume(int value) {
	return value;
}

int main() {
	int values[3] = {1, 2, 3};
	return consume(values);
}
