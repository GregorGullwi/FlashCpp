template <typename T>
struct Wrapper {
	union Nested;
};

template <typename T>
union Wrapper<T>::Nested {
	int a;
	char b[4];
};

int main() {
	Wrapper<int>::Nested value{};
	value.a = 0x01020304;
	return sizeof(Wrapper<int>::Nested) == 4 &&
		   offsetof(Wrapper<int>::Nested, a) == 0 &&
		   offsetof(Wrapper<int>::Nested, b) == 0 &&
		   value.a == 0x01020304 ? 0 : 1;
}
