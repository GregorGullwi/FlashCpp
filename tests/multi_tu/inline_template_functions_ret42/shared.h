inline int incrementSharedValue(int value) {
	return value + 1;
}

template <typename T>
T doubleSharedValue(T value) {
	return value + value;
}

int firstTranslationUnitValue();
