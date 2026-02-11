// Test: trailing return type on out-of-line template member function definition
// Pattern: auto ClassName<T>::method(params) -> RetType { ... }

template<typename T>
struct Container {
	using iterator = T*;
	using const_iterator = const T*;

	iterator begin();
	iterator end();
	const_iterator cbegin() const;
};

template<typename T>
auto Container<T>::begin() -> iterator {
	return nullptr;
}

template<typename T>
auto Container<T>::end() -> iterator {
	return nullptr;
}

template<typename T>
auto Container<T>::cbegin() const -> const_iterator {
	return nullptr;
}

int main() {
	Container<int> c;
	auto b = c.begin();
	auto e = c.end();
	auto cb = c.cbegin();
	if (b == nullptr && e == nullptr && cb == nullptr)
		return 7;
	return 0;
}
