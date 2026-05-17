template <bool B>
struct Flag {
	static constexpr int value = B ? 1 : 0;
};

template <typename T>
struct TraitUse {
	static int first() { return Flag<__is_same(T, T)>::value; }
	static int second() { return Flag<__is_same(T, const T)>::value; }
};

struct MyStruct {};

template <typename T>
using Alias = T;

template <typename T>
bool checksPass() {
	return TraitUse<T>::first() != TraitUse<T>::second();
}

int main() {
	return (checksPass<int>() &&
			checksPass<long>() &&
			checksPass<MyStruct>() &&
			checksPass<Alias<char>>())
		? 0
		: 1;
}
