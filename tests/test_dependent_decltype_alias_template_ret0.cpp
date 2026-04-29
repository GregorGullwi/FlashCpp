template <typename T>
T&& declval();

template <typename T>
struct decay_selector {
	using type = T;
};

template <typename T>
struct decay {
	using type = typename decay_selector<T>::type;
};

template <typename T>
using decay_t = typename decay<T>::type;

struct common_type_probe {
	template <typename T, typename U>
	using cond_t = decltype(true ? declval<T>() : declval<U>());

	template <typename T, typename U>
	static decay_t<cond_t<T, U>> test(int);
};

int main() {
	return 0;
}
