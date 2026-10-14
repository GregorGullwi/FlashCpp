struct Receiver {
	long value;
	Receiver(const long& argument) noexcept : value(argument) {}
};

int main() {
	short source = 7;
	Receiver* allocated = new Receiver(source);
	const bool correct = allocated->value == 7;
	delete allocated;
	return correct ? 0 : 1;
}
