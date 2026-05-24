// Regression: partial-specialization nested out-of-line member-template attachment
// must resolve source-member -> instantiated-stub identity first, then apply
// same-name overload disambiguation against that identity-resolved stub.
template<class T>
struct PartialOverload;

template<class T>
struct PartialOverload<T*> {
	template<class U>
	int eval(T left, U right);

	template<class U>
	int eval(U a, U b, U c);
};

template<class T>
template<class U>
int PartialOverload<T*>::eval(T left, U right) {
	return left + static_cast<T>(right);
}

template<class T>
template<class U>
int PartialOverload<T*>::eval(U a, U b, U c) {
	return static_cast<T>(a + b + c);
}

int main() {
	PartialOverload<int*> p;
	if (p.eval(40, 2L) != 42) {
		return 1;
	}
	if (p.eval(10, 20, 12) != 42) {
		return 2;
	}
	return 0;
}
