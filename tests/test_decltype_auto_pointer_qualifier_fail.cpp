int main() {
	int value = 42;
	decltype(auto)* ptr = &value;
	return *ptr;
}
