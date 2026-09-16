int destructor_count = 0;

template <typename T>
struct OwnerWithDestructor {
	~OwnerWithDestructor();
};

template <typename T>
OwnerWithDestructor<T>::~OwnerWithDestructor() {
	++destructor_count;
}

template <typename T>
struct OwnerWithNestedConstructor {
	template <typename U>
	struct Nested {
		Nested();
		int value;
	};
};

template <typename T>
template <typename U>
OwnerWithNestedConstructor<T>::Nested<U>::Nested()
	: value(sizeof(T) + sizeof(U)) {}

template <typename T>
struct OwnerWithDefaultedDestructor {
	~OwnerWithDefaultedDestructor();
};

template <typename T>
OwnerWithDefaultedDestructor<T>::~OwnerWithDefaultedDestructor() = default;

template <typename T>
struct OwnerWithDefaultedNestedConstructor {
	template <typename U>
	struct Nested {
		Nested();
		int value = 17;
	};
};

template <typename T>
template <typename U>
OwnerWithDefaultedNestedConstructor<T>::Nested<U>::Nested() = default;

int main() {
	{
		OwnerWithDestructor<int> value;
	}
	if (destructor_count != 1) {
		return 1;
	}

	OwnerWithNestedConstructor<int>::Nested<char> nested;
	if (nested.value != sizeof(int) + sizeof(char)) {
		return 2;
	}

	OwnerWithDefaultedDestructor<long> defaulted_destructor;
	OwnerWithDefaultedNestedConstructor<long>::Nested<short> defaulted_nested;
	if (defaulted_nested.value != 17) {
		return 3;
	}
	(void)defaulted_destructor;
	return 0;
}
