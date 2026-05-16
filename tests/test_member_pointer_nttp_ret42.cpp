struct S {
	int member;
};

template <int S::* P>
struct member_pointer_tag {
	static constexpr int value = 1;
};

template <>
struct member_pointer_tag<&S::member> {
	static constexpr int value = 42;
};

int main() {
	return member_pointer_tag<&S::member>::value;
}
