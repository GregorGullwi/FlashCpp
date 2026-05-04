// Regression: copy-initializing a class-template variable from a value of the
// same class template (where one side is the pattern type and the other is an
// instantiated specialization in a deferred member body) used to trip
// "Cannot use copy initialization with explicit constructor" because of an
// `explicit` converting constructor declared on the same template that took a
// forward-declared type.  This reproduces the libstdc++ reverse_iterator
// `reverse_iterator __tmp = *this;` pattern.

struct Other;

template<typename _Iter>
struct ReverseIter {
	_Iter current;

	ReverseIter() : current() {}
	explicit ReverseIter(_Iter __x) : current(__x) {}
	ReverseIter(const ReverseIter& __x) : current(__x.current) {}

	// explicit converting ctor that takes a forward-declared type — this is
	// the shape that previously fooled the converting-ctor scan into thinking
	// the same-template copy required an explicit converting ctor.
	explicit ReverseIter(const Other& d);

	// Copy *this into a local of the same template; previously rejected with
	// "Cannot use copy initialization with explicit constructor".
	ReverseIter copy_self() const {
		ReverseIter __tmp = *this;
		return __tmp;
	}
};

int main() {
	int a = 1;
	ReverseIter<int*> ri(&a);
	(void)ri;
	return 0;
}

