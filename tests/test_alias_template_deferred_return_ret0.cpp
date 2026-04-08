// Regression test: deferred alias-template materialization in template return types.

struct size_two {
	char data[2];
};

struct size_forty {
	char data[40];
};

template <bool B>
struct choose_value {
	using type = size_two;
};

template <>
struct choose_value<true> {
	using type = size_forty;
};

template <bool B>
using choose_value_t = typename choose_value<B>::type;

template <bool B>
choose_value_t<B> make_value() {
	return {};
}

int main() {
	auto forty = make_value<true>();
	auto two = make_value<false>();
	return (sizeof(forty) == 40 && sizeof(two) == 2) ? 0 : 1;
}
