template <typename T>
struct holder {
	using value_type = T;
};

template <template <typename...> class Holder, typename... Args>
concept HasSizedValueType = requires {
	typename Holder<Args...>;
	requires sizeof(typename Holder<Args...>::value_type) == sizeof(long long);
};

template <typename Default, template <typename...> class Holder, typename... Args>
struct detector {
	static constexpr int value = 1;
};

template <typename Default, template <typename...> class Holder, typename... Args>
	requires HasSizedValueType<Holder, Args...>
struct detector<Default, Holder, Args...> {
	static constexpr int value = 8;
};

int main() {
	return detector<int, holder, long long>::value;
}
