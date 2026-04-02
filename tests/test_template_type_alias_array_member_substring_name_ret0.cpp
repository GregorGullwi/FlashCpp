template<typename T>
struct TypeNested {
	using type = T[2];
};

template<typename T>
struct wrapper {
	typename TypeNested<T>::type elems;
	int tail;
};

int main() {
	wrapper<int> value = {1, 2, 7};
	return (value.elems[0] == 1 &&
			value.elems[1] == 2 &&
			value.tail == 7)
			   ? 0
			   : 1;
}
