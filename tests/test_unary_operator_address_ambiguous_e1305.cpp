// A member and a non-member operator& with the same signature tie during unary
// address-of overload resolution, so it must report AmbiguousOperatorOverload.

struct Addressable {
	int operator&();
};

int operator&(Addressable&);

int main() {
	Addressable value;
	return &value;
}
