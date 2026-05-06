template<typename T>
struct Identity {
	using type = T;
};

template<int N>
using BadAlias = typename Identity<N>::type;

BadAlias<7> value;

int main() {
	return 0;
}
