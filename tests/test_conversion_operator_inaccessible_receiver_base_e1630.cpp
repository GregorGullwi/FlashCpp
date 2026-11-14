struct ValueBase {
	operator int() const {
		return 0;
	}
};

struct PrivateDerived : private ValueBase {};

int main() {
	PrivateDerived value;
	return value;
}
