// A member alias target must preserve each dependent pack's boundary when it
// expands multiple packs into one template-id.
template<class... T>
struct Pack {
	static constexpr int count = sizeof...(T);
};

template<class... A>
struct Alias {
	template<class... B>
	using type = Pack<A..., B...>;
};

template<class... A>
struct Owner {
	template<class... B>
	struct Box {
		typename Alias<A...>::template type<B...> field;
	};
};

int main() {
	Owner<int, char>::Box<short> one{};
	if (one.field.count != 3) return 1;

	Owner<int, char>::Box<> empty_second{};
	if (empty_second.field.count != 2) return 2;

	Owner<short>::Box<int, char, long> full_second{};
	if (full_second.field.count != 4) return 3;

	Owner<int>::Box<char> one_and_one{};
	if (one_and_one.field.count != 2) return 4;

	Owner<>::Box<short, long> empty_first{};
	if (empty_first.field.count != 2) return 5;

	Owner<>::Box<> empty_both{};
	if (empty_both.field.count != 0) return 6;

	return 42;
}
