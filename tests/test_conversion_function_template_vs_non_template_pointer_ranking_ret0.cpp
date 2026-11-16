// Object-pointer conversion templates and non-template pointer operators must
// be ranked together: an exact template result beats a different pointer
// conversion, a template can deduce the cv-qualified pointee, and a non-template
// wins when the result types are indistinguishable.

static int first_target = 1;
static int second_target = 2;

struct VoidPointerTemplateConversion {
	operator void*() const {
		return &first_target;
	}

	template<class U>
	operator U*() const {
		return static_cast<U*>(&second_target);
	}
};

struct PointerTemplateConversion {
	operator int*() const {
		return &first_target;
	}

	template<class U>
	operator U*() const {
		return static_cast<U*>(&second_target);
	}
};

int main() {
	int* exact = VoidPointerTemplateConversion{};
	if (exact != &second_target) {
		return 1;
	}
	int* tie = PointerTemplateConversion{};
	if (tie != &first_target) {
		return 2;
	}
	const int* const_pointee = PointerTemplateConversion{};
	if (const_pointee != &second_target) {
		return 3;
	}
	return 0;
}
