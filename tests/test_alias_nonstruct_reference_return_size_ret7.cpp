template<typename T>
struct remove_reference {
	using type = T;
};

template<typename T>
struct remove_reference<T&> {
	using type = T;
};

using plain_int = typename remove_reference<int&>::type;

plain_int& alias_ref(plain_int& value) {
	return value;
}

int main() {
	int value = 7;
	plain_int& ref = alias_ref(value);
	return ref;
}
