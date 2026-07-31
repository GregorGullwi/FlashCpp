// Regression: returning T& from static_cast<U&>(object).member must materialize
// the selected member's address, including when the cast produces an
// address-holding temporary used as the member base.
template <class T>
struct Holder {
	T first;
};

template <class T>
struct Wrapper {
	using Self = Wrapper<T>;
	Holder<T> inner;
};

template <class T>
T& get_first(Wrapper<T>& wrapper) {
	using Self = typename Wrapper<T>::Self;
	return static_cast<Self&>(wrapper).inner.first;
}

int main() {
	Wrapper<int> wrapper{{42}};
	int& member = get_first(wrapper);
	member = 7;

	Wrapper<long long> wide_wrapper{{84}};
	long long& wide_member = get_first(wide_wrapper);
	wide_member = 9;

	return wrapper.inner.first == 7 && wide_wrapper.inner.first == 9 ? 0 : 1;
}
