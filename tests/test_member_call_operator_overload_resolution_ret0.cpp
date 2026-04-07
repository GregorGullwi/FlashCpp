struct Iter {
	int pos;
};

int operator-(Iter lhs, Iter rhs) {
	return lhs.pos - rhs.pos;
}

struct Range {
	using iterator = Iter;

	iterator begin() {
		return iterator{1};
	}

	iterator end() {
		return iterator{4};
	}
};

int main() {
	Range range{};
	return (range.end() - range.begin() == 3) ? 0 : 1;
}
