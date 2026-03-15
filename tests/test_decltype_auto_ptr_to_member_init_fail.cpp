struct S {
	int value;
};

int main() {
	S s{42};
	int S::* member = &S::value;
	decltype(auto) result = s.*member;
	return result;
}
