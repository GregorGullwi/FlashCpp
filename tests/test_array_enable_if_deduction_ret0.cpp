typedef unsigned long size_t;

template<bool Cond, typename Type = void>
struct enable_if { };

template<typename Type>
struct enable_if<true, Type> {
	using type = Type;
};

template<typename Type>
struct is_pickable {
	static constexpr bool value = true;
};

template<typename Type, size_t N>
typename enable_if<is_pickable<Type>::value, int>::type
pickArray(Type (&)[N], Type (&)[N]) {
	return static_cast<int>(N);
}

int main() {
	int left[3] = {1, 2, 3};
	int right[3] = {4, 5, 6};
	return pickArray(left, right) == 3 ? 0 : 1;
}
