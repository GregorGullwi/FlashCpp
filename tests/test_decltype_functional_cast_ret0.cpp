int getValue() {
	return 5;
}

struct Pair {
	int first;
	long second;

	Pair(int a, long b)
		: first(a), second(b) {
	}
};

Pair makePair() {
	return Pair(1, 2);
}

int main() {
	int paren_value = decltype(getValue())(11);
	int brace_value = decltype(getValue()){13};
	Pair paren_pair = decltype(makePair())(17, 19);
	Pair brace_pair = decltype(makePair()){23, 29};

	if (paren_value != 11)
		return 1;
	if (brace_value != 13)
		return 2;
	if (paren_pair.first != 17 || paren_pair.second != 19)
		return 3;
	if (brace_pair.first != 23 || brace_pair.second != 29)
		return 4;
	return 0;
}
