int main() {
	int left = 5;
	int right = 11;
	int* left_ptr = &left;
	int* right_ptr = &right;

	long result = (left < 0) ? *left_ptr : *right_ptr;
	return static_cast<int>(result - 11L);
}
