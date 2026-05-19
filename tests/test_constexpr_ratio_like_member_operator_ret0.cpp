struct Big {
	unsigned long long upper;
	unsigned long long lower;

	constexpr bool operator<(Big rhs) const {
		if (upper != rhs.upper) {
			return upper < rhs.upper;
		}
		return lower < rhs.lower;
	}
};

constexpr Big bigMultiply(unsigned long long left, unsigned long long right) {
	const unsigned long long leftLow = left & 0xFFFFFFFFULL;
	const unsigned long long leftHigh = left >> 32;
	const unsigned long long rightLow = right & 0xFFFFFFFFULL;
	const unsigned long long rightHigh = right >> 32;

	const unsigned long long productLow = leftLow * rightLow;
	const unsigned long long productMiddle1 = leftHigh * rightLow;
	const unsigned long long productMiddle2 = leftLow * rightHigh;
	const unsigned long long productHigh = leftHigh * rightHigh;

	const unsigned long long carry = ((productLow >> 32) + (productMiddle1 & 0xFFFFFFFFULL) + (productMiddle2 & 0xFFFFFFFFULL)) >> 32;

	return {productHigh + (productMiddle1 >> 32) + (productMiddle2 >> 32) + carry, productLow + (productMiddle1 << 32) + (productMiddle2 << 32)};
}

constexpr bool ratioLess(long long nx1, long long dx1, long long nx2, long long dx2) {
	if (nx1 >= 0 && nx2 >= 0) {
		return bigMultiply(static_cast<unsigned long long>(nx1), static_cast<unsigned long long>(dx2))
			< bigMultiply(static_cast<unsigned long long>(nx2), static_cast<unsigned long long>(dx1));
	}
	return nx1 < nx2;
}

constexpr bool lessWithParams(unsigned long long lhs, unsigned long long rhs) {
	return lhs < rhs;
}

constexpr bool nonNegative(long long lhs, long long rhs) {
	return lhs >= 0 && rhs >= 0;
}

constexpr bool castLess(long long lhs, long long rhs) {
	return static_cast<unsigned long long>(lhs) < static_cast<unsigned long long>(rhs);
}

constexpr bool castBigLess(long long lhs, long long rhs) {
	return bigMultiply(1, static_cast<unsigned long long>(lhs)) < bigMultiply(1, static_cast<unsigned long long>(rhs));
}

constexpr bool crossBigLess(long long nx1, long long dx1, long long nx2, long long dx2) {
	return bigMultiply(static_cast<unsigned long long>(nx1), static_cast<unsigned long long>(dx2))
		< bigMultiply(static_cast<unsigned long long>(nx2), static_cast<unsigned long long>(dx1));
}

static_assert(bigMultiply(1, 2).lower == 2);
static_assert(bigMultiply(1, 3).lower == 3);
static_assert(bigMultiply(1, 2) < bigMultiply(1, 3));
static_assert(lessWithParams(2, 3));
static_assert(nonNegative(1, 1));
static_assert(castLess(2, 3));
static_assert(castBigLess(2, 3));
static_assert(crossBigLess(1, 3, 1, 2));
static_assert(ratioLess(1, 3, 1, 2));

int main() {
	return 0;
}
