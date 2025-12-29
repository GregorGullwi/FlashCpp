struct false_type {
	static constexpr bool value = false;
};

struct true_type {
	static constexpr bool value = true;
};

template<bool B, typename T = void>
struct enable_if { };

template<typename T>
struct enable_if<true, T> {
	using type = T;
};

template<typename... Bn>
struct all_false {
	static constexpr bool value = false;
};

template<>
struct all_false<> {
	static constexpr bool value = true;
};

template<typename First, typename... Rest>
struct all_false<First, Rest...> {
	static constexpr bool value = !First::value && all_false<Rest...>::value;
};

template<typename... Bn>
auto or_fn(int) -> typename enable_if<all_false<Bn...>::value, false_type>::type;

template<typename... Bn>
auto or_fn(...) -> true_type;

template<typename... Bn>
struct my_or : decltype(or_fn<Bn...>(0)) { };

int main() {
	using mixed = my_or<false_type, true_type>;
	return mixed::value ? 42 : 1;
}
