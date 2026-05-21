template <typename A, typename B>
struct Pair {
	A first;
	B second;
};

template <typename T>
struct Provider {
	struct Node {
		template <typename U>
		using Apply = Pair<T, U>;
	};
};

template <typename T>
struct Use {
	template <typename U>
	using Alias = typename Provider<T>::Node::template Apply<U>;
};

int main() {
	Use<char>::Alias<int>* value = (Use<char>::Alias<int>*)0;
	return sizeof(value->first) == sizeof(char) && sizeof(value->second) == sizeof(int) ? 0 : 1;
}
