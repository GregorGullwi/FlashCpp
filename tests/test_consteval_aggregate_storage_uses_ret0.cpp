struct Wide {
	long long value;
	double real;
	int tail;
};
consteval Wide makeWide() { return {9000000000LL, 2.5, 17}; }
Wide returnWide() { return makeWide(); }
int checkWide(Wide value) {
	return value.value == 9000000000LL && value.real == 2.5 && value.tail == 17 ? 0 : 1;
}
int main() {
	if (checkWide(makeWide()) != 0) return 1;
	if (checkWide(returnWide()) != 0) return 2;
	const Wide& reference = makeWide();
	return reference.value == 9000000000LL && reference.real == 2.5 && reference.tail == 17 ? 0 : 3;
}
