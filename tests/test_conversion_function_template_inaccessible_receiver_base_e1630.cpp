struct ValueBase {
	template<class T>
	operator T() const {
		return T{};
	}
};

struct PrivateDerived : private ValueBase {};

int main() {
	PrivateDerived value;
	return value;
}
