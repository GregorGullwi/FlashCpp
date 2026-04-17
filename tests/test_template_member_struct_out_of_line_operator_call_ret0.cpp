// Regression test: member struct templates must carry out-of-line operator()
// member templates through instantiation.

template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		template<typename V>
		U operator()(V value) const;
	};
};

template<typename T>
template<typename U>
template<typename V>
U Outer<T>::Inner<U>::operator()(V value) const {
	return static_cast<U>(value) + 40;
}

int main() {
	Outer<int>::Inner<int> add;
	return add(2) - 42;
}
