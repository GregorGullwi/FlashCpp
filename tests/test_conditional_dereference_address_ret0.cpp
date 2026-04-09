int main() {
	int left = 5;
	int right = 11;
	int* left_ptr = &left;
	int* right_ptr = &right;

	bool pick_left = false;
	int* ptr = &(pick_left ? *left_ptr : *right_ptr);
	if (*ptr != 11)
		return 1;
	*ptr = 17;
	if (right != 17)
		return 2;
	if (left != 5)
		return 3;
	return 0;
}
