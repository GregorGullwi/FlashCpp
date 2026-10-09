// A conversion operator must be selected when a named lvalue initializes a
// const lvalue reference or an rvalue reference. Return 0 only when each call
// applies the conversion and preserves the converted value. A non-const lvalue
// reference cannot bind that temporary; that rejection is
// test_conversion_operator_nonconst_lvalue_reference_e1704.cpp.

struct B {
	int value;
};

struct Base {
	int value;
};

struct Derived : Base {};

template <class T>
struct TBase {
	int value;
};

template <class T>
struct TDerived : TBase<T> {};

struct SameSource {
	operator B() const {
		B b;
		b.value = 17;
		return b;
	}
};

struct DerivedSource {
	operator Derived() const {
		Derived d;
		d.value = 21;
		return d;
	}
};

struct TemplateSource {
	operator TDerived<int>() const {
		TDerived<int> d;
		d.value = 33;
		return d;
	}
};

struct SameTemplateSource {
	operator TBase<int>() const {
		TBase<int> b;
		b.value = 19;
		return b;
	}
};

struct RvalueSource {
	operator B() const {
		B b;
		b.value = 7;
		return b;
	}
};

int choose_same(const B& b) { return b.value; }
int choose_derived(const Base& b) { return b.value; }
int choose_template(const TBase<int>& b) { return b.value; }
int choose_same_template(const TBase<int>& b) { return b.value; }
int choose_rvalue(B&& b) { return b.value; }

int call_same(const SameSource s) { return choose_same(s); }
int call_derived(DerivedSource s) { return choose_derived(s); }
int call_template(TemplateSource s) { return choose_template(s); }
int call_same_template(SameTemplateSource s) { return choose_same_template(s); }
int call_rvalue(RvalueSource s) { return choose_rvalue(s); }

int main() {
	int r = 0;
	r |= call_same(SameSource{}) == 17 ? 0 : 1;
	r |= call_derived(DerivedSource{}) == 21 ? 0 : 2;
	r |= call_template(TemplateSource{}) == 33 ? 0 : 4;
	r |= call_same_template(SameTemplateSource{}) == 19 ? 0 : 8;
	r |= call_rvalue(RvalueSource{}) == 7 ? 0 : 16;
	return r;
}
