struct S {
	int x;
	int y;

	constexpr S(int a, int b)
		: x(a), y(b) {}
};

constexpr int get_member(const S& value, int S::* member_ptr) {
	return value.*member_ptr;
}

constexpr int sum_members(const S& value, int S::* first, int S::* second) {
	return value.*first + value.*second;
}

constexpr int result =
	(get_member(S{3, 7}, &S::x) == 3 &&
	 get_member(S{3, 7}, &S::y) == 7 &&
	 sum_members(S{5, 11}, &S::x, &S::y) == 16)
		? 0
		: 1;

static_assert(result == 0);

int main() {
	return result;
}
