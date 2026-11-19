template<class T>
struct DependentConversionBase {
	T data[3];
	template<class U = void>
	operator T*() { return data; }
};

template<class T>
struct DependentConversionDerived : DependentConversionBase<T> {};

int main() {
	DependentConversionDerived<int> value;
	value.data[1] = 42;
	return value[1] == 42 ? 0 : 1;
}
