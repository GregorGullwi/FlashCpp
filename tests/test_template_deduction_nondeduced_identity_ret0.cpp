template <typename T>
struct identity {
	using type = T;
};

template <typename T>
int select(typename identity<T>::type first, T second) {
	return first == 1 && second == 2 ? 0 : 1;
}

int main() {
	// T is deduced from the second parameter.  The nested-name-specifier in
	// identity<T>::type is a non-deduced context, so the first int argument must
	// not conflict with the second parameter's deduced type.
	return select(1, 2);
}
