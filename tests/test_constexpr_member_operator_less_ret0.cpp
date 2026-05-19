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

constexpr Big makeBig(unsigned long long value) {
	return {0, value};
}

static_assert(makeBig(2) < makeBig(3));

int main() {
	return 0;
}
