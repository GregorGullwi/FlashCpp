template<int N>
struct Chain;

template<int N>
using PreviousChain = Chain<N - 1>;

template<int N>
struct Chain : PreviousChain<N> {};

template<>
struct Chain<0> {
	template<class T>
	operator T() const {
		return T{41};
	}
};

int read(short value) {
	return value;
}

template<class T>
int read_dependent(T value) {
	return read(value);
}

int main() {
	if (read(Chain<0>{}) != 41) {
		return 1;
	}
	return read_dependent(Chain<12>{}) == 41 ? 0 : 2;
}
