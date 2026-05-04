struct Other;

struct Big {
	long value;

	Big() = default;
	Big(long v)
		: value(v) {}

	explicit Big(const Other&);

	Big operator~() const {
		return Big{~value};
	}

	Big operator-() const {
		return operator~() + 1;
	}

	friend Big operator+(Big lhs, long rhs) {
		lhs.value += rhs;
		return lhs;
	}
};

int main() {
	Big a(5);
	Big b = -a;
	return b.value == ((~5L) + 1) ? 0 : 1;
}
