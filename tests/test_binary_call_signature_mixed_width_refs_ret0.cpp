struct Calculator {
	short operator+(int& right) const { return static_cast<short>(right + 1); }
	long long operator+(long long&& right) const { return right + 2; }
};

int main() {
	Calculator calculator;
	int small = 40;
	long long large = 40;
	static_assert(sizeof(decltype(calculator + small)) == sizeof(short));
	static_assert(sizeof(decltype(calculator + 40LL)) == sizeof(long long));
	if (calculator + small != 41) return 1;
	if (calculator + static_cast<long long&&>(large) != 42) return 2;
	return 0;
}
