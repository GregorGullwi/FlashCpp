struct A {
	int value;
};

struct B {
	int value;
};

template <auto P>
struct member_pointer_tag {
	static constexpr int value = 0;
};

template <>
struct member_pointer_tag<&A::value> {
	static constexpr int value = 1;
};

template <>
struct member_pointer_tag<&B::value> {
	static constexpr int value = 2;
};

int main() {
	return member_pointer_tag<&A::value>::value * 10 +
		member_pointer_tag<&B::value>::value - 12;
}
