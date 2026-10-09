// A conversion operator to a class target must be selected for a named lvalue
// argument, not only for a temporary. Return 0 only when each named-lvalue call
// applies the conversion and preserves the converted value.

struct Base {
	int value;
};

struct Derived : Base {};

struct DirectSource {
	operator Base() const {
		Base b;
		b.value = 7;
		return b;
	}
};

struct DerivedSource {
	operator Derived() const {
		Derived d;
		d.value = 11;
		return d;
	}
};

struct ConstSource {
	operator Base() const {
		Base b;
		b.value = 3;
		return b;
	}
};

template <class T>
struct TBase {
	int value;
};

template <class T>
struct TDerived : TBase<T> {};

struct TemplateSource {
	operator TDerived<int>() const {
		TDerived<int> d;
		d.value = 13;
		return d;
	}
};

int choose_direct(Base b) { return b.value; }
int choose_tail(Base b) { return b.value; }
int choose_const(Base b) { return b.value; }
int choose_template(TBase<int> b) { return b.value; }

int call_direct(DirectSource s) { return choose_direct(s); }
int call_tail(DerivedSource s) { return choose_tail(s); }
int call_const(const ConstSource s) { return choose_const(s); }
int call_template(TemplateSource s) { return choose_template(s); }

int main() {
	int r = 0;
	r |= call_direct(DirectSource{}) == 7 ? 0 : 1;
	r |= call_tail(DerivedSource{}) == 11 ? 0 : 2;
	r |= call_const(ConstSource{}) == 3 ? 0 : 4;
	r |= call_template(TemplateSource{}) == 13 ? 0 : 8;
	return r;
}
