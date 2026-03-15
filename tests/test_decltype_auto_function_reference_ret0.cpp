decltype(auto) forward_ref(int& value) {
	return (value);
}

int main() {
	int value = 1;
	forward_ref(value) = 7;
	return value == 7 ? 0 : 1;
}
