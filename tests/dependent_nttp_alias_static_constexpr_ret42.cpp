template<int Value>
struct Tag {
	static constexpr int value = Value;
};

template<typename T>
struct Holder {
	static constexpr int value = sizeof(T) + 34;
};

template<typename T>
using Alias = Tag<Holder<T>::value>;

int main() {
	return Alias<long long>::value;
}
