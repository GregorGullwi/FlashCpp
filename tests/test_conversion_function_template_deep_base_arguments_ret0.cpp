struct Root {
	template<class T>
	operator T() const {
		return T{37};
	}
};

template<int N>
struct ConversionBase;

template<int N>
using PreviousBase = ConversionBase<N - 1>;

template<int N>
struct ConversionBase : PreviousBase<N> {};

template<>
struct ConversionBase<0> : Root {};

int read(short value) {
	return value;
}

template<class T>
int read_dependent(T value) {
	return read(value);
}

int main() {
	return read_dependent(ConversionBase<96>{}) == 37 ? 0 : 1;
}
