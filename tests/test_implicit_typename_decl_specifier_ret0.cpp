// C++20 P0634R3: omitted `typename` is valid in decl-specifier-seq and
// trailing-return-type contexts, including namespace-scope function return
// types and class-member declarations.

template <class T>
struct SizeHolder {
	using size_type = unsigned long long;
	using type = T;
};

template <class T>
struct CharHolder {
	using size_type = unsigned short;
	using type = T;
};

struct Payload {
	int a;
	char b;
};

template <class Container>
Container::size_type erase_like(Container& cont, const typename Container::type& value) {
	(void)cont;
	(void)value;
	return 1;
}

template <class Container>
auto trailing_size() -> Container::size_type {
	return 2;
}

template <class T>
struct DerivedMember {
	SizeHolder<T>::type value{};
	CharHolder<T>::size_type count{};
};

int main() {
	SizeHolder<int> ints{};
	CharHolder<Payload> chars{};
	DerivedMember<short> nested{};
	nested.value = 7;
	nested.count = 3;

	const auto erased_ints = erase_like(ints, 0);
	const auto erased_chars = erase_like(chars, Payload{});
	const auto trail = trailing_size<SizeHolder<long long>>();

	return (erased_ints == 1 && erased_chars == 1 && trail == 2 && nested.value == 7 && nested.count == 3)
		? 0
		: 1;
}
