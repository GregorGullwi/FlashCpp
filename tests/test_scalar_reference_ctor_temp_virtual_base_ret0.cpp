struct Receiver {
	long value;
	Receiver(const long& argument) noexcept : value(argument) {}
};

struct Derived : virtual Receiver {
	Derived(short source) : Receiver(source) {}
};

int main() {
	Derived value(7);
	return value.value == 7 ? 0 : 1;
}
