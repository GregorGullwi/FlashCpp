template<int N>
struct Cached {
	static constexpr int value = N;
};

using WarmedCached = Cached<0>;

template<int N>
struct Chain : Chain<N - 1> {
	Cached<0> cached;
};

template<>
struct Chain<0> {
	Cached<0> cached;
};

int main() {
	Chain<39> chain{};
	return chain.cached.value;
}
