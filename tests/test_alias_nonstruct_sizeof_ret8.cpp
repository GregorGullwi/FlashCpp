template<typename T>
struct remove_reference {
	using type = T;
};

template<typename T>
struct remove_reference<T&> {
	using type = T;
};

int main() {
	using plain_long_long = typename remove_reference<long long&>::type;
	return sizeof(plain_long_long);
}
