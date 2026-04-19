int& max_ref_direct(int& left, int& right) {
	return left > right ? left : right;
}

int& max_ref_cached_condition(int& left, int& right) {
	bool take_left = left > right;
	return take_left ? left : right;
}

int main() {
	int low = 1;
	int high = 2;

	int& direct = max_ref_direct(low, high);
	direct = 10;
	if (high != 10) {
		return 1;
	}

	int& cached = max_ref_cached_condition(low, high);
	cached = 20;
	if (high != 20) {
		return 2;
	}

	return 42;
}
