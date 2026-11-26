// The `template` disambiguator and a non-template outer class both reach the
// same nested-name-specifier owner resolution. Initializers depend on sizeof so
// storage that never receives the definition's value reads back zero.

template <class T>
struct Outer {
	template <class U>
	struct Inner {
		static int data;
	};
};

template <class T>
template <class U>
int Outer<T>::template Inner<U>::data = static_cast<int>(sizeof(T) + sizeof(U) * 100);

struct PlainOuter {
	template <class U>
	struct Inner {
		static int data;
	};
};

template <class U>
int PlainOuter::Inner<U>::data = static_cast<int>(sizeof(U) * 7);

int main() {
	if (Outer<char>::Inner<int>::data != 401) {
		return 1;
	}
	if (Outer<long long>::template Inner<short>::data != 208) {
		return 2;
	}
	if (PlainOuter::Inner<char>::data != 7) {
		return 3;
	}
	if (PlainOuter::Inner<long long>::data != 56) {
		return 4;
	}
	return 0;
}
