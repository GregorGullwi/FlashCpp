// Same rule for a reference operand: the pointee's deferred base does not make
// the reference itself trivially copyable, so the answer must stay definite.

struct Base {};

template<class T> struct Derived : T {
	static_assert(__is_trivially_copyable(Derived&), "reference is not trivially copyable");
};

int main() {
	Derived<Base> value;
	return 0;
}
