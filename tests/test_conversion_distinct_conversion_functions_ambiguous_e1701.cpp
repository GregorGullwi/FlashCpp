// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences are ordered
// by their second standard conversion sequence only when they contain the same
// conversion function or constructor. These candidates reach their parameters
// through two different conversion operators, so short -> int being a promotion
// may not rank the int candidate ahead of the exact long long candidate; the
// call is ambiguous.
struct SplitConversion {
	operator short() const {
		return 1;
	}

	operator long long() const {
		return 2;
	}
};

int choose(int value) {
	return value;
}

int choose(long long value) {
	return static_cast<int>(value) + 10;
}

int main() {
	return choose(SplitConversion{});
}
