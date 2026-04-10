int getValue() {
	return 5;
}

int main() {
	int paren_value = decltype(getValue())(11);
	int brace_value = decltype(getValue()){13};

	if (paren_value != 11)
		return 1;
	if (brace_value != 13)
		return 2;
	return 0;
}
