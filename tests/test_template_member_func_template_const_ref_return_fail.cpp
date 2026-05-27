template<typename T>
struct Box {
	T value;

	template<typename U>
	const T& get();
};

template<typename T>
template<typename U>
const T& Box<T>::get() {
	return value;
}

int& (Box<int>::*pmf)() = &Box<int>::template get<int>;

int main() {
	return 0;
}
