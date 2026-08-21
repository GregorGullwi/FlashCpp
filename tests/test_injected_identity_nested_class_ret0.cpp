template <class T>
struct Holder {
	template <class U>
	struct Box {
		U value;
	};

	struct Nested {
		typename Holder<T>::template Box<T>
		pass(typename Holder<T>::template Box<T> value) {
			typename Holder<T>::template Box<T> local = value;
			return local;
		}
	};
};

int main() {
	Holder<int>::Box<int> value{42};
	Holder<int>::Nested nested{};
	return nested.pass(value).value == 42 ? 0 : 1;
}
