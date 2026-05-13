struct Payload {
	int a;
	short b;
	char c;
};

using PayloadAlias = Payload;
using PayloadRefAlias = PayloadAlias&;

struct Holder {
	static PayloadAlias object;
	static PayloadRefAlias ref;
};

PayloadAlias Holder::object = {7, 3, 1};
PayloadRefAlias Holder::ref = Holder::object;

int main() {
	return sizeof(Holder::object) == sizeof(Payload) &&
			sizeof(Holder::ref) == sizeof(Payload) &&
			Holder::object.a == 7
		? 0
		: 1;
}
