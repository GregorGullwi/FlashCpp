int main() {
	int value = 37;
	int* pointer = &value;
	int** pointer_pointer = &pointer;
	int* materialized = *pointer_pointer;
	if (*materialized != 37)
		return 1;
	int** materialized_pointer_pointer = &materialized;
	int* materialized_again = *materialized_pointer_pointer;
	return *materialized_again == 37 ? 0 : 2;
}
