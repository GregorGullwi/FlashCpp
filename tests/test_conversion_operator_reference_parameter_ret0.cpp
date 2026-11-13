// A non-template conversion operator returning an lvalue reference must be
// viable for lvalue reference parameters, for temporaries and named lvalues,
// and must lose to the less cv-qualified reference.

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

void set_seven(int& value) {
	value = 7;
}

int choose(int&) {
	return 1;
}

int choose(const int&) {
	return 2;
}

int read(const int& value) {
	return value;
}

struct Pad {
	char byte;
};

struct Base {
	int value;
};

struct Derived : Pad, Base {
	short tag;
};

struct DerivedConversion {
	Derived derived;

	operator Derived&() {
		return derived;
	}
};

void set_base(Base& value) {
	value.value = 9;
}

int main() {
	set_seven(LvalueRefConversion{});
	int& reference = LvalueRefConversion{};
	if (reference != 7) {
		return 1;
	}
	LvalueRefConversion named;
	set_seven(named);
	if (reference != 7) {
		return 2;
	}
	if (choose(LvalueRefConversion{}) != 1) {
		return 3;
	}
	if (choose(ConstRefConversion{}) != 2) {
		return 4;
	}
	if (read(ConstRefConversion{}) != 84) {
		return 5;
	}
	if (read(LvalueRefConversion{}) != 7) {
		return 6;
	}
	DerivedConversion derived_conversion;
	set_base(derived_conversion);
	if (derived_conversion.derived.value != 9) {
		return 7;
	}
	return 0;
}
