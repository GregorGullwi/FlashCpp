struct ValueBase {
	operator int() const {
		return 7;
	}
};

struct Middle : ValueBase {};

struct Derived : private Middle {
public:
	using Middle::operator int;
};

int main() {
	Derived value;
	int converted = value;
	return converted == 7 ? 0 : 1;
}
