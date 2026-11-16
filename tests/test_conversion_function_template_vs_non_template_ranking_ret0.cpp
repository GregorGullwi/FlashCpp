// Conversion function templates and non-template conversion functions must be
// ranked together: an exact template conversion beats a promotion non-template
// conversion, a direct lvalue-reference result beats a temporary, and a
// non-template wins when the conversion sequences are otherwise indistinguishable.

struct PromotionTemplateConversion {
	operator short() const {
		return 1;
	}

	template<class U>
	operator U() const {
		return U{2};
	}
};

struct ExactTemplateConversion {
	operator int() const {
		return 1;
	}

	template<class U>
	operator U() const {
		return U{2};
	}
};

struct DirectReferenceTemplateConversion {
	operator int() const {
		return 1;
	}

	template<class U>
	operator U&() const {
		static U value{2};
		return value;
	}
};

struct DirectReferenceNonTemplateConversion {
	operator int&() const {
		static int value{1};
		return value;
	}

	template<class U>
	operator U&() const {
		static U value{2};
		return value;
	}
};

int take_int(int value) {
	return value;
}

int read_reference(const int& value) {
	return value;
}

int main() {
	if (static_cast<int>(PromotionTemplateConversion{}) != 2) {
		return 1;
	}
	int initialized = PromotionTemplateConversion{};
	if (initialized != 2) {
		return 2;
	}
	if (take_int(PromotionTemplateConversion{}) != 2) {
		return 3;
	}
	if (static_cast<int>(ExactTemplateConversion{}) != 1) {
		return 4;
	}
	int exact_initialized = ExactTemplateConversion{};
	if (exact_initialized != 1) {
		return 5;
	}
	if (take_int(ExactTemplateConversion{}) != 1) {
		return 6;
	}
	if (read_reference(DirectReferenceTemplateConversion{}) != 2) {
		return 7;
	}
	if (read_reference(DirectReferenceNonTemplateConversion{}) != 1) {
		return 8;
	}
	return 0;
}
