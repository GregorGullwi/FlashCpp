struct Receiver {
	long value;
	Receiver(const int& argument) : value(argument + 10) {}
	Receiver(const long& argument) : value(argument + 100) {}
};

struct MemberHolder {
	Receiver receiver;
	MemberHolder(short argument) : receiver(argument) {}
};

struct BaseHolder : Receiver {
	BaseHolder(short argument) : Receiver(argument) {}
};

struct DelegatingHolder {
	long value;
	DelegatingHolder(const int& argument) : value(argument + 10) {}
	DelegatingHolder(const long& argument) : value(argument + 100) {}
	DelegatingHolder(short argument, char) : DelegatingHolder(argument) {}
};

int main() {
	MemberHolder member(7);
	BaseHolder base(8);
	DelegatingHolder delegated(static_cast<short>(9), 'x');
	return member.receiver.value == 17 && base.value == 18 && delegated.value == 19 ? 0 : 1;
}
