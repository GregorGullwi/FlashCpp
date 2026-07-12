template<typename T>
struct Apply {};

template<typename T>
struct Classify {
	static constexpr int value = 0;
};

template<typename U>
struct Classify<Apply<U>> {
	static constexpr int value = 1;
};

int main() {
	return Classify<Apply<int>*>::value;
}
