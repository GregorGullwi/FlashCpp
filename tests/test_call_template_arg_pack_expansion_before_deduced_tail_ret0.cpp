int add3(int a, int b, int tail) {
	return a + b + tail;
}

int tailKind(long) {
	return 12;
}

template <typename T>
int typeCode();

template <>
int typeCode<char>() {
	return 10;
}

template <>
int typeCode<short>() {
	return 20;
}

template <>
int typeCode<long>() {
	return 99;
}

template <typename T, typename... Rest, typename U>
int sumRestCodes(T, Rest..., U tail) {
	return add3(typeCode<Rest>()..., tailKind(tail));
}

int main() {
	return sumRestCodes<int, char, short>(1, 'a', static_cast<short>(2), 0L) == 42 ? 0 : 1;
}
