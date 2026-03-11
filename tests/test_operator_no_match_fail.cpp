// Direct non-dependent operator no-match should be rejected.

struct NoPlus {
	int value;
};

int main() {
	NoPlus lhs{1};
	NoPlus rhs{2};
	auto sum = lhs + rhs;
	return sum.value;
}