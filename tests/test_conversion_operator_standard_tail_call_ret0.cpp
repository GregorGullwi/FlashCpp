// A conversion operator may be followed by a standard conversion tail: a
// derived-to-base conversion for a class target, or a member-pointer base
// adjustment. The evaluated call must both select the overload and materialize
// the conversion, not merely rank it in decltype. Return 0 only when every
// evaluated call applies the tail and preserves the converted value.

template <class T>
struct Base {
	int value;
};

template <class T>
struct Derived : Base<T> {};

// Conversion operator returns a class derived from the target: user conversion
// followed by a derived-to-base tail.
struct DerivedSource {
	operator Derived<int>() const {
		Derived<int> d;
		d.value = 42;
		return d;
	}
};

// Conversion operator returns the target directly.
struct DirectSource {
	operator Base<int>() const {
		Base<int> b;
		b.value = 7;
		return b;
	}
};

int choose_class(Base<int> b) { return b.value; }
int choose_direct(Base<int> b) { return b.value; }

// Overload selection: the conversion-requiring overload must beat the ellipsis.
int choose_overload(Base<int>) { return 0; }
int choose_overload(...) { return 1; }

// Conversion operator returns a member-function-pointer whose owner is a base
// of the selected parameter's owner.
template <class T>
struct MfpBase {
	int get() noexcept { return 1; }
};

template <class T>
struct MfpDerived : MfpBase<T> {};

template <class T>
using MfpBaseFunction = int (MfpBase<T>::*)() noexcept;

template <class T>
using MfpDerivedFunction = int (MfpDerived<T>::*)() noexcept;

template <class T>
struct MfpSource {
	operator MfpBaseFunction<T>() const { return &MfpBase<T>::get; }
};

int choose_mfp(MfpDerivedFunction<int>) { return 5; }

int main() {
	int r = 0;
	r |= choose_class(DerivedSource{}) == 42 ? 0 : 1;
	r |= choose_direct(DirectSource{}) == 7 ? 0 : 2;
	r |= choose_overload(DerivedSource{}) == 0 ? 0 : 4;
	r |= choose_mfp(MfpSource<int>{}) == 5 ? 0 : 8;
	return r;
}
