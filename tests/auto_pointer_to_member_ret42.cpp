struct S {
	int x;
};

int main() {
	S s{42};
	int S::* ptr = &S::x;
	auto value = s.*ptr;
	return value;
}
