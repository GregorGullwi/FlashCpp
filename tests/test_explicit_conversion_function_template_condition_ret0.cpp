// An explicit conversion-function template can be used by an explicit cast,
// and explicit(false) permits implicit copy-initialization.

struct ExplicitTemplateConversion {
	template<class U>
	explicit(true) operator U() const {
		return U{1};
	}
};

struct ImplicitTemplateConversion {
	template<class U>
	explicit(false) operator U() const {
		return U{2};
	}
};

int main() {
	int explicit_cast = static_cast<int>(ExplicitTemplateConversion{});
	if (explicit_cast != 1) {
		return 1;
	}
	int implicit_copy = ImplicitTemplateConversion{};
	if (implicit_copy != 2) {
		return 2;
	}
	return 0;
}
