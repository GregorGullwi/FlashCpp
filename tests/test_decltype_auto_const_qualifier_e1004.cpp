int main() {
	int value = 42;
	const decltype(auto) copy = value;
	return copy;
}
