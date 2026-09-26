// Conditional pointer common types must preserve qualification at every
// pointer level while keeping the intermediate pointer const.
int select_value(int**) {
	return -1;
}

int select_value(const int* const* pointer) {
	return **pointer;
}

int main() {
	int value = 42;
	int* inner = &value;
	int** mutable_pointer = &inner;
	const int* const* qualified_pointer = mutable_pointer;
	return select_value((1 == 1) ? mutable_pointer : qualified_pointer);
}
