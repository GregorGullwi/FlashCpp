void probe(int);

struct yes {
	static constexpr bool value = true;
};

struct no {
	static constexpr bool value = false;
};

template<class T, class = void>
struct has_probe : no {};

template<class T>
struct has_probe<T, decltype(probe(static_cast<int>(sizeof(typename T::type))), void())> : yes {};

struct good {
	using type = char;
};

struct bad {};

int main() {
	return has_probe<good>::value && !has_probe<bad>::value ? 0 : 1;
}
