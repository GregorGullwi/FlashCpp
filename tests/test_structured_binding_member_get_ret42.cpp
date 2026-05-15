// Test member get() function template for tuple-like structured binding
// Per [dcl.struct.bind]/3: if E has a member named get, use e.get<I>()
// Expected return: 42 (10 + 32)

namespace std {
	template <typename T>
	struct tuple_size;

	template <unsigned long I, typename T>
	struct tuple_element;
} // namespace std

struct Point {
	int x;
	int y;

	template <unsigned long I>
	int get() const;
};

template <>
int Point::get<0>() const { return x; }

template <>
int Point::get<1>() const { return y; }

namespace std {
	template <>
	struct tuple_size<Point> {
		static constexpr unsigned long value = 2;
	};

	template <>
	struct tuple_element<0, Point> {
		using type = int;
	};

	template <>
	struct tuple_element<1, Point> {
		using type = int;
	};
} // namespace std

int main() {
	Point p;
	p.x = 10;
	p.y = 32;
	auto [a, b] = p;
	return a + b;  // expected: 42
}
