struct Pair {
	int first;
	int second;
};

int main() {
	Pair source = {1, 2};
	auto [value, extra] = source;
	{
		Pair inner_source = {7, 8};
		auto [value, extra] = inner_source;
		if (value != 7 || extra != 8) return 1;
	}
	if (value != 1 || extra != 2) return 2;
	return 0;
}