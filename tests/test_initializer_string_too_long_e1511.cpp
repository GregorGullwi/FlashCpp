// A string literal used to initialize a char array that is too small for the
// literal and its terminator is ill-formed and must report
// InitializerStringTooLongForArray.

int main() {
	char text[2] = "abc";
	return text[0];
}
