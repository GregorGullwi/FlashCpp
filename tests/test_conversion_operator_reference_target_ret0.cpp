// A conversion operator that returns a reference must bind reference targets to
// the referenced object instead of materializing a copy of the produced value.

struct LvalueRefConversion {
	operator int&() const {
		static int value{42};
		return value;
	}
};

struct ConstRefConversion {
	operator const int&() const {
		static int value{84};
		return value;
	}
};

int& get_lvalue_reference() {
	return LvalueRefConversion{};
}

int main() {
	int& implicit_binding = LvalueRefConversion{};
	int& explicit_binding = static_cast<int&>(LvalueRefConversion{});
	if (implicit_binding != 42 || explicit_binding != 42) {
		return 1;
	}
	explicit_binding = 7;
	if (implicit_binding != 7) {
		return 2;
	}
	const int& const_from_mutable = LvalueRefConversion{};
	if (const_from_mutable != 7) {
		return 3;
	}
	const int& const_implicit = ConstRefConversion{};
	const int& const_explicit = static_cast<const int&>(ConstRefConversion{});
	if (const_implicit != 84 || const_explicit != 84) {
		return 4;
	}
	int& returned = get_lvalue_reference();
	if (returned != 7) {
		return 5;
	}
	returned = 13;
	if (get_lvalue_reference() != 13) {
		return 6;
	}
	return 0;
}
