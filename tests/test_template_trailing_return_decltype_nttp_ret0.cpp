template <int N>
auto value() -> decltype(N) {
	return N;
}

int main() {
	return value<42>() == 42 ? 0 : 1;
}
