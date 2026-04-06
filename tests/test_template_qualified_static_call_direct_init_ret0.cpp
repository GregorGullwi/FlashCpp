struct Box {
	int value;
	Box(int v) : value(v) {}
};

template<typename T>
struct Helper {
	static T make(int v) { return T(v); }
};

template<typename T>
T build(int value) {
	T result(Helper<T>::make(value));
	return result;
}

int main() {
	Box box = build<Box>(9);
	return box.value == 9 ? 0 : 1;
}
