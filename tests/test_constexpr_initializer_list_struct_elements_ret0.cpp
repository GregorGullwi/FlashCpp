namespace std {
template <typename T>
class initializer_list {
public:
	const T* first_;
	const T* last_;

	constexpr initializer_list(const T* f, const T* l) noexcept : first_(f), last_(l) {}

	constexpr const T* begin() const noexcept {
		return first_;
	}

	constexpr const T* end() const noexcept {
		return last_;
	}
};
} // namespace std

struct Point {
	int x;
	int y;
};

constexpr int sum_x(std::initializer_list<Point> points) {
	int total = 0;
	for (Point point : points) {
		total += point.x;
	}
	return total;
}

int main() {
	static_assert(sum_x({Point{10, 1}, Point{20, 2}, Point{30, 3}}) == 60);
	return 0;
}
