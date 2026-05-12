// Test full tuple-like structured binding protocol with mixed element types
// Expected return: 42 (10 + 20 + 12)

namespace std {
template <typename T>
struct tuple_size;

template <unsigned long I, typename T>
struct tuple_element;
} // namespace std

struct Payload {
	int bonus;
};

struct MyTriple {
	int first;
	double second;
	Payload third;
};

// Specialize tuple_size
namespace std {
template <>
struct tuple_size<MyTriple> {
	static constexpr unsigned long value = 3;
};

template <>
struct tuple_element<0, MyTriple> {
	using type = int;
};

template <>
struct tuple_element<1, MyTriple> {
	using type = double;
};

template <>
struct tuple_element<2, MyTriple> {
	using type = Payload;
};
} // namespace std

// Define get<> function template
template <unsigned long I>
typename std::tuple_element<I, MyTriple>::type get(const MyTriple& p);

// Explicit specializations for get<0>, get<1>, and get<2>
template <>
int get<0>(const MyTriple& p) {
	return p.first;
}

template <>
double get<1>(const MyTriple& p) {
	return p.second;
}

template <>
Payload get<2>(const MyTriple& p) {
	return p.third;
}

int main() {
	MyTriple p;
	p.first = 10;
	p.second = 20.0;
	p.third.bonus = 12;

	// This should use the tuple-like protocol:
	auto [a, b, c] = p;

	return a + static_cast<int>(b) + c.bonus;
}
