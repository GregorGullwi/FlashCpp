template <typename T>
struct AliasSized {
	using value_type = T;
	using pointer_type = T*;
	using const_pointer_type = const T*;
	using const_type = const T;
	using array_type = T[5];
	using const_array_type = const T[5];

	static_assert(sizeof(value_type) == sizeof(T));
	static_assert(sizeof(pointer_type) == sizeof(T*));
	static_assert(sizeof(const_pointer_type) == sizeof(const T*));
	static_assert(sizeof(const_type) == sizeof(T));
	static_assert(sizeof(array_type) == sizeof(T) * 5);
	static_assert(sizeof(const_array_type) == sizeof(const T) * 5);
	static_assert(sizeof(array_type[10]) == sizeof(T) * 50);

	value_type value;
};

struct Payload {
	int a;
	short b;
	char c;
};

int main() {
	AliasSized<Payload> wrapped{{7, 3, 1}};
	return sizeof(typename AliasSized<Payload>::value_type) == sizeof(Payload) &&
			wrapped.value.a == 7 &&
			wrapped.value.b == 3 &&
			wrapped.value.c == 1
		? 0
		: 1;
}
