// A brace-initialized new-expression that narrows a double to int is a
// list-initialization narrowing and must report the shared
// NarrowingConversionInListInitialization owner rather than an unlocated string.

int main() {
	double value = 1.5;
	int* pointer = new int{value};
	delete pointer;
	return 0;
}
