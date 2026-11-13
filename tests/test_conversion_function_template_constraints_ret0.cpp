template<class T>
concept AtLeastTwoBytes = sizeof(T) >= 2;

template<class T>
concept FourBytes = sizeof(T) == 4;

struct Conversion {
	template<AtLeastTwoBytes T>
	operator T() const {
		return T{17};
	}

	template<AtLeastTwoBytes T>
	operator T() const requires FourBytes<T> {
		return T{41};
	}
};

int read(int value) {
	return value;
}

short read_short(short value) {
	return value;
}

int main() {
	if (read(Conversion{}) != 41 || read_short(Conversion{}) != 17) {
		return 1;
	}
	return 0;
}
