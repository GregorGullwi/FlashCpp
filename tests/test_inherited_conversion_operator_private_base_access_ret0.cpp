struct ValueBase {
	int value;

	operator int() const {
		return value;
	}
};

struct TemplateValueBase {
	template<class T>
	operator T() const {
		return T{37};
	}
};

struct PrivateDerived : private ValueBase {
	PrivateDerived() {
		value = 42;
	}

	int read_ordinary() const {
		return *this;
	}
};

struct PrivateTemplateDerived : private TemplateValueBase {
	int read_template() const {
		return *this;
	}
};

int main() {
	PrivateDerived ordinary;
	PrivateTemplateDerived templated;
	return ordinary.read_ordinary() == 42 && templated.read_template() == 37 ? 0 : 1;
}
