int main() {
	int left = 1;
	int right = 2;
	(true ? static_cast<int&&>(left) : static_cast<int&&>(right)) = 3;
	return 0;
}
