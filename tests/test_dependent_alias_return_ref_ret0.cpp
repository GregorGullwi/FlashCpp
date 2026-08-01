template <typename T>
struct remove_reference {
	using type = T;
};

template <typename T>
struct remove_reference<T&> {
	using type = T;
};

template <typename T>
struct remove_reference<T&&> {
	using type = T;
};

template <typename T>
using remove_reference_t = typename remove_reference<T>::type;

template <typename T>
using lvalue_reference_t = remove_reference_t<T>&;

template <typename T>
using rvalue_reference_t = remove_reference_t<T>&&;

template <typename T>
lvalue_reference_t<T> lvalue_identity(lvalue_reference_t<T> value) {
	return value;
}

template <typename T>
rvalue_reference_t<T> rvalue_identity(remove_reference_t<T>& value) {
	return static_cast<rvalue_reference_t<T>>(value);
}

struct WideValue {
	int first;
	long long second;
};

int main() {
	int value = 42;
	int& lvalue = lvalue_identity<int&>(value);
	lvalue = 43;

	WideValue wide{7, 900};
	WideValue&& rvalue = rvalue_identity<WideValue>(wide);
	rvalue.second = 901;

	return value == 43 && wide.second == 901 ? 0 : 1;
}
