template <class T>
struct Holder {
	template <class U>
	struct Box {
		U value;
	};

	template <class U>
	struct Late {
		typename Holder<T>::template Box<T>
		pass(typename Holder<T>::template Box<T> value) {
			return value;
		}
	};
};

int main() {
	Holder<int>::Box<int> value{42};
	Holder<int>::Late<char> late{};
	return late.pass(value).value == 42 ? 0 : 1;
}
