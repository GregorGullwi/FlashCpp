template <class T>
struct bool_trait {
	static constexpr bool value = false;
};

template <>
struct bool_trait<int> {
	static constexpr bool value = true;
};

using aliased_int = int;

static_assert(bool_trait<aliased_int>::value);

int main() {
	return bool_trait<aliased_int>::value ? 0 : 1;
}
