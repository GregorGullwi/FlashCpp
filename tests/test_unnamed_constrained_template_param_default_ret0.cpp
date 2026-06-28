template <class T>
concept LocalIntegral = __is_integral(T);

template <LocalIntegral = int>
struct box {
	static constexpr int value = 7;
};

template <LocalIntegral>
struct tag {
	static constexpr int value = 4;
};

int main() {
	return box<>::value - tag<long long>::value - 3;
}
