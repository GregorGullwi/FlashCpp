template <decltype(nullptr) P>
struct null_tag {
	static constexpr int value = 1;
};

template <>
struct null_tag<nullptr> {
	static constexpr int value = 42;
};

int main() {
	if (null_tag<nullptr>::value != 42) return 1;
	return 0;
}
