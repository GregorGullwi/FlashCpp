template<typename T>
struct remove_reference {
	using type = T;
};

template<typename T>
struct remove_reference<T&> {
	using type = T;
};

template<typename T>
using remove_reference_t = typename remove_reference<T>::type;

template<typename T>
using const_ref = const remove_reference_t<T>&;

int readValue(const_ref<int&> value) {
	return value;
}

int main() {
	int n = 0;
	return readValue(n);
}
