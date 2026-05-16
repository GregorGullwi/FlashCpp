// Test: constexpr range-based for over std::initializer_list<T> (C++20)
//
// The constexpr evaluator should allow begin()/end()-based iteration when the
// iterator pointers only carry snapshot state from the synthesized backing
// array.
//
// Expected exit code: 0

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

constexpr int sum(std::initializer_list<int> values) {
	int total = 0;
	for (int value : values) {
		total += value;
	}
	return total;
}

int main() {
	constexpr int result = sum({1, 2, 3, 4});
	static_assert(result == 10, "initializer_list range-for should sum all elements");
	return result - 10;
}
