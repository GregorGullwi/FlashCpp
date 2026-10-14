struct Receiver {
	long value;
	Receiver(const long& argument) noexcept : value(argument) {}
};

int main() {
	short source = 7;
	Receiver copied = source;
	return copied.value == 7 ? 0 : 1;
}
