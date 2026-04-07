template <typename T>
struct Holder {
	template <typename U>
	struct Box {
		using value_type = U;
		value_type value;
	};

	typename Holder<T>::template Box<T>::value_type run() {
		typename Holder<T>::template Box<T> box{42};
		return box.value;
	}
};

int main() {
	Holder<int> h;
	return h.run();
}
