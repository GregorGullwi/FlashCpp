constexpr int pick_inclass(long) {
	return 17;
}

template <typename T>
struct InClassCarrier {
	static constexpr int value = pick_inclass(sizeof(T) + 1);
};

constexpr int pick_inclass(int) {
	return 91;
}

int pick_ool(long) {
	return 23;
}

template <typename T>
struct OutOfLineCarrier {
	static int value;
};

template <typename T>
int OutOfLineCarrier<T>::value = pick_ool(sizeof(T) + 1);

int pick_ool(int) {
	return 77;
}

int main() {
	if (InClassCarrier<char>::value != 17) {
		return 1;
	}
	if (InClassCarrier<long long>::value != 17) {
		return 2;
	}
	if (OutOfLineCarrier<char>::value != 23) {
		return 3;
	}
	if (OutOfLineCarrier<long long>::value != 23) {
		return 4;
	}
	return 0;
}
