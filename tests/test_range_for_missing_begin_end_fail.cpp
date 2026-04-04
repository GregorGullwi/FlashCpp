struct Foo {
	int value;
};

int main() {
	Foo f{42};
	for (auto x : f) {
		return x;
	}
	return 0;
}
