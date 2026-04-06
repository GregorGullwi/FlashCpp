template <typename T>
struct RangeBox {
	const T* first;
	const T* last;

	RangeBox(const T* begin, const T* end)
		: first(begin), last(end) {
	}

	int size() const {
		return static_cast<int>(last - first);
	}
};

using IntRangeBox = RangeBox<int>;

int main() {
	int values[3] = {4, 5, 6};
	IntRangeBox full(values, values + 3);
	IntRangeBox tail(values + 1, values + 3);
	return (full.size() == 3 && tail.size() == 2) ? 0 : 1;
}
