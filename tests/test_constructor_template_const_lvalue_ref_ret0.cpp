struct Payload {
	char bytes[3];
};

struct Receiver {
	template<class T> Receiver(const T&) {}
};

struct NamedReceiver {
	template<class T> NamedReceiver(const T& value) { static_cast<void>(value); }
};

int main() {
	Payload payload{};
	Receiver from_int(7);
	Receiver from_double(2.5);
	Receiver from_payload(payload);
	Receiver from_payload_temporary(Payload{});
	NamedReceiver from_named_int(9);
	static_cast<void>(from_int);
	static_cast<void>(from_double);
	static_cast<void>(from_payload);
	static_cast<void>(from_payload_temporary);
	static_cast<void>(from_named_int);
	return 0;
}
