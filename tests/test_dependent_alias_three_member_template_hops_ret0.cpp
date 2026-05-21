template <typename A, typename B>
struct Pair {
	A first;
	B second;
};

template <typename T>
struct Traits {
	template <typename U>
	struct Box {
		template <typename V>
		struct Mid {
			template <typename W>
			using Rebind = Pair<W, int>;
		};
	};
};

template <typename T, typename U>
using Alias = typename Traits<T>::template Box<U>::template Mid<long>::template Rebind<T>;

int main() {
	Alias<char, short> v{};
	return sizeof(v.first) == sizeof(char) && sizeof(v.second) == sizeof(int) ? 0 : 1;
}
