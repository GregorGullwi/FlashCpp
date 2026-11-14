struct ValueBase {
	template<class Tag = void>
	operator int() const {
		return 7;
	}
};

struct Middle : ValueBase {
	using ValueBase::operator int;
};

struct InaccessibleMiddle : private Middle {};

int main() {
	InaccessibleMiddle value;
	int converted = value;
	return converted;
}
