struct Receiver {
	long value;
	Receiver(const long& argument) noexcept : value(argument) {}
	Receiver(short source, bool tag) : Receiver(source) {
		if (tag) {
			value += 0;
		}
	}
};

int main() {
	Receiver value(7, true);
	return value.value == 7 ? 0 : 1;
}
