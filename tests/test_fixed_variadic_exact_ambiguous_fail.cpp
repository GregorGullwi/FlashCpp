// C++20 [over.ics.rank]: when every supplied argument binds to a named
// parameter, a trailing ellipsis does not distinguish otherwise identical
// conversion sequences.

int select(int value, const char* text) {
	return value + (text[0] == 'x');
}

int select(int value, const char* text, ...) {
	return value + (text[0] == 'x');
}

int main() {
	return select(41, "x");
}
