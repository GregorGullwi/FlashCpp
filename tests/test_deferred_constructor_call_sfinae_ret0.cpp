template<class U>
char probe(U*);

struct yes {
	static constexpr bool value = true;
};

struct no {
	static constexpr bool value = false;
};

template<class T, class = void>
struct has_probe : no {};

template<class T>
struct has_probe<T, decltype(probe(T{}), void())> : yes {};

int main() {
	return has_probe<char*>::value ? 0 : 1;
}
