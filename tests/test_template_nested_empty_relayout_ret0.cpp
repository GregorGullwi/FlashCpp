template<typename T>
struct Wrapper {
	struct Nested;
	Nested nested;
};

template<typename T>
struct Wrapper<T>::Nested {
};

int main() {
	return sizeof(Wrapper<int>) == 1 &&
		   alignof(Wrapper<int>) == 1 &&
		   sizeof(Wrapper<int>::Nested) == 1 ? 0 : 1;
}
