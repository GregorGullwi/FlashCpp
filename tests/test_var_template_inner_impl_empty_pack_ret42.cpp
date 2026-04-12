template <typename... Types>
struct pack_trait {
	static constexpr int value = sizeof...(Types) == 0 ? 42 : 7;
};

template <typename... Types>
inline constexpr int pack_trait_v = pack_trait<Types...>::value;

int main() {
	int value = pack_trait_v<>;
	return value >= 0 ? 42 : 0;
}
