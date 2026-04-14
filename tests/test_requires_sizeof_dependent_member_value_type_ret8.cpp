template <typename T>
struct identity {
	using type = T;
};

template <typename Prefix, template <typename> class Wrap, typename T>
struct holder {
	using value_type = typename Wrap<T>::type;
};

template <template <typename, template <typename> class, typename> class Holder,
		  typename Prefix,
		  template <typename> class Wrap,
		  typename T>
concept HasSizedValueType = requires {
	typename Holder<Prefix, Wrap, T>;
	requires sizeof(typename Holder<Prefix, Wrap, T>::value_type) == sizeof(long long);
};

template <typename Default,
		  template <typename, template <typename> class, typename> class Holder,
		  typename Prefix,
		  template <typename> class Wrap,
		  typename T>
struct detector {
	static constexpr int value = 1;
};

template <typename Default,
		  template <typename, template <typename> class, typename> class Holder,
		  typename Prefix,
		  template <typename> class Wrap,
		  typename T>
	requires HasSizedValueType<Holder, Prefix, Wrap, T>
struct detector<Default, Holder, Prefix, Wrap, T> {
	static constexpr int value = 8;
};

int main() {
	return detector<int, holder, char, identity, long long>::value;
}
