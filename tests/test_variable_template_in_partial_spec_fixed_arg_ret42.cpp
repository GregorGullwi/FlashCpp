template <class Type>
constexpr bool TypeHasFourBytes = sizeof(Type) == 4;

template <class Type, bool = TypeHasFourBytes<Type>>
struct Selector {
	static constexpr bool value = false;
};

template <class Type>
struct Selector<Type, true> {
	static constexpr bool value = TypeHasFourBytes<Type>;
};

int main() {
	return Selector<int>::value ? 42 : 0;
}
