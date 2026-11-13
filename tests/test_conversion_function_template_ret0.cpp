// A conversion function template whose bare return type is its single deduced
// type parameter must participate in explicit and implicit conversions.

struct TemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

struct ExplicitTemplateConversion {
	template<class U>
	explicit operator U() const {
		return U{};
	}
};

struct RvalueTemplateConversion {
	template<class U>
	operator U() && {
		return U{};
	}
};

int main() {
	int a = static_cast<int>(TemplateConversion{});
	int b = static_cast<int>(ExplicitTemplateConversion{});
	int c = static_cast<int>(RvalueTemplateConversion{});
	int d = TemplateConversion{};
	const TemplateConversion const_template;
	int e = static_cast<int>(const_template);
	int f = const_template;
	if (a != 0 || b != 0 || c != 0 || d != 0 || e != 0 || f != 0) {
		return 1;
	}
	return 0;
}
