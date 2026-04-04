struct Base {
	char c;
};

template<typename T>
struct DerivedOuter : Base {
	struct Nested;
	Nested nested;
};

template<typename T>
struct DerivedOuter<T>::Nested {
	int value;
};

int main() {
	if (sizeof(DerivedOuter<int>) != 8) {
		return 1;
	}

	return 0;
}
