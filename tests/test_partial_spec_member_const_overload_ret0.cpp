// Regression: partial-specialization member parsing must preserve trailing
// const/volatile on ordinary member functions. Without that, instantiated
// const/non-const overloads both look non-const and non-const receivers become
// ambiguous (MSVC <tuple>/_Get_rest).
template<class T>
struct Wrap;

template<class T>
struct Wrap<T*> {
	T* value;

	Wrap(T* v) : value(v) {}

	Wrap& Get_rest() {
		return *this;
	}
	const Wrap& Get_rest() const {
		return *this;
	}
};

int main() {
	int x = 1;
	Wrap<int*> a(&x);
	Wrap<int*>& nonconst_ref = a.Get_rest();
	if (nonconst_ref.value != &x) {
		return 1;
	}

	const Wrap<int*> ca(&x);
	const Wrap<int*>& const_ref = ca.Get_rest();
	if (const_ref.value != &x) {
		return 2;
	}

	return 0;
}
