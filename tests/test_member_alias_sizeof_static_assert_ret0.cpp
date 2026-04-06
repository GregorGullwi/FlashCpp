template <typename T>
struct AliasSized {
	using value_type = T;

	static_assert(sizeof(value_type) == sizeof(T));

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
