struct Receiver {
	long value;
	Receiver(const long& argument) noexcept : value(argument) {}
};

struct Middle : Receiver {};

struct Derived : Middle {
	Derived(short source) : Middle(source) {}
};

int main() {
	Derived value(7);
	return value.value == 7 ? 0 : 1;
}
