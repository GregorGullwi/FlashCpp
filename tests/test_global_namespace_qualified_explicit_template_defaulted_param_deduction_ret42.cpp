template<typename T>
struct Score;

template<>
struct Score<int> {
	static constexpr int value = 1;
};

struct Marker {};

template<>
struct Score<Marker> {
	static constexpr int value = 42;
};

namespace ns {
	template<typename T, typename U = int>
	int deduceSecondParam(T, U) {
		return Score<U>::value;
	}
}

int main() {
	Marker marker;
	return ::ns::deduceSecondParam<double>(1.0, marker);
}
