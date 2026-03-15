int make_value() {
	return 42;
}

int main() {
	decltype(auto) value = make_value();
	return 42 - value;
}
