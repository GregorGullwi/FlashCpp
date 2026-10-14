struct Receiver {
	long value;
	Receiver(const long& argument) noexcept : value(argument) {}
};

struct Holder {
	Receiver receiver;
	Holder(short source) : receiver(source) {}
};

int main() {
	Holder holder(7);
	return holder.receiver.value == 7 ? 0 : 1;
}
