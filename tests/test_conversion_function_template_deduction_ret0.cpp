template<class First, class Second>
struct Pair {
	First first;
	Second second;
};

struct PairConversion {
	template<class A, class B>
	operator Pair<A, B>() const {
		return Pair<A, B>{7, 13};
	}
};

struct DefaultConversion {
	template<class T, class Tag = T, int Count = 29>
	operator T() const {
		return static_cast<T>(Tag{Count});
	}
};

int read(Pair<short, long> value) {
	return value.first * 2 + value.second;
}

int read_int(int value) {
	return value;
}

int main() {
	if (read(PairConversion{}) != 27) {
		return 1;
	}
	if (read_int(DefaultConversion{}) != 29) {
		return 2;
	}
	return 0;
}
