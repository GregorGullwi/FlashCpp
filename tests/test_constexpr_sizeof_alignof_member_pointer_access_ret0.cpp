struct S {
	int x;
	double y;
};

constexpr double S::* double_member = &S::y;
constexpr int S::* int_member = &S::x;

constexpr int testSizeof() {
	S s{1, 2.0};
	S* ps = &s;
	return sizeof(s.*double_member)
		+ sizeof(ps->*double_member)
		+ sizeof(s.*int_member);
}

constexpr int testAlignof() {
	S s{1, 2.0};
	S* ps = &s;
	return alignof(s.*double_member)
		+ alignof(ps->*double_member)
		+ alignof(s.*int_member);
}

constexpr int testParameter(double S::* member) {
	S s{1, 2.0};
	return sizeof(s.*member) + alignof(s.*member) - static_cast<int>(sizeof(double) + alignof(double));
}

static_assert(testSizeof() == sizeof(double) + sizeof(double) + sizeof(int));
static_assert(testAlignof() == alignof(double) + alignof(double) + alignof(int));
static_assert(testParameter(double_member) == 0);

int main() {
	return 0;
}
