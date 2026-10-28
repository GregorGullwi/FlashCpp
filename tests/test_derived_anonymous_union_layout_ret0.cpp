struct Root {
	long long root;
};

struct Key : Root {
	union {
		short small;
		long long wide;
	};
};

int main() {
	Key value{};
	value.root = 12345678901LL;
	value.small = 7;
	if (value.small != 7 || value.root != 12345678901LL) {
		return 1;
	}
	value.wide = 9876543210LL;
	return value.wide == 9876543210LL && value.root == 12345678901LL ? 0 : 1;
}
