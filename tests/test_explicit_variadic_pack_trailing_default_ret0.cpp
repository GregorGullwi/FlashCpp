int classifyValues(char, short) {
	return 23;
}

int classifyValues(char, short, int) {
	return 99;
}

template<typename... Ts, typename Tail>
int packBeforeTail(Ts... values, Tail) {
	return classifyValues(values...);
}

int main() {
	if (packBeforeTail<char, short>(1, 2, 7L) != 23) {
		return 1;
	}

	if (packBeforeTail<char, short, int>(1, 2, 3, 7L) != 99) {
		return 2;
	}

	return 0;
}
