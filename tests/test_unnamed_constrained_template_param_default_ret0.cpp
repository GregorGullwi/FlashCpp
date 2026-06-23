#include <concepts>

template <std::integral = int>
struct box {
	static constexpr int value = 7;
};

template <std::integral>
struct tag {
	static constexpr int value = 4;
};

int main() {
	return box<>::value - tag<long long>::value - 3;
}
