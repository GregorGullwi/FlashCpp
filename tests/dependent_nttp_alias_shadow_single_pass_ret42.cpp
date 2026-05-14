constexpr int N = 1;

template<int Value>
struct Tag {
	static constexpr int value = Value;
};

template<int N>
using Alias = Tag<N + 40>;

int main() {
	return Alias<2>::value;
}
