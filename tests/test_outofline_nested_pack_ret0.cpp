#pragma pack(push, 1)
template <typename T>
struct Wrapper {
	struct Nested;
};

template <typename T>
struct Wrapper<T>::Nested {
	char c;
	int x;
};
#pragma pack(pop)

int main() {
	Wrapper<int>::Nested value{};
	value.c = 7;
	value.x = 42;
	return sizeof(Wrapper<int>::Nested) == 5 &&
		   offsetof(Wrapper<int>::Nested, x) == 1 &&
		   value.c == 7 &&
		   value.x == 42 ? 0 : 1;
}
