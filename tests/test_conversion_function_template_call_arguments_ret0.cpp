// Conversion function templates participate in call-argument viability for
// by-value parameters and for lvalue-reference-returning results bound to lvalue
// reference parameters.

struct ValueTemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

struct PointerTemplateConversion {
	template<class U>
	operator U*() const {
		return nullptr;
	}
};

struct ReferenceTemplateConversion {
	template<class U>
	operator U&() const {
		static U value{42};
		return value;
	}
};

int take_int(int value) {
	return value;
}

int take_pointer(int* value) {
	return value == nullptr ? 0 : 1;
}

void set_seven(int& value) {
	value = 7;
}

int read(const int& value) {
	return value;
}

int main() {
	if (take_int(ValueTemplateConversion{}) != 0) {
		return 1;
	}
	ValueTemplateConversion named;
	if (take_int(named) != 0) {
		return 2;
	}
	if (take_pointer(PointerTemplateConversion{}) != 0) {
		return 3;
	}
	if (take_pointer(ValueTemplateConversion{}) != 0) {
		return 4;
	}
	set_seven(ReferenceTemplateConversion{});
	int& reference = ReferenceTemplateConversion{};
	if (reference != 7) {
		return 5;
	}
	if (read(ReferenceTemplateConversion{}) != 42) {
		return 6;
	}
	return 0;
}
