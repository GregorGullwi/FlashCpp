int main() {
	int value = 42;
	decltype(auto)& ref = value;
	return ref;
}
