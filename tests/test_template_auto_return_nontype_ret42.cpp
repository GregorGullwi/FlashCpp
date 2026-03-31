template <int N>
auto getValue() {
	return N;
}

int main() {
	return getValue<42>();
}
