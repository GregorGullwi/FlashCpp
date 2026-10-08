template<class T>
struct Base {
	int get() noexcept { return 1; }
};

template<class T>
struct Derived : Base<T> {};

template<class T>
using BaseFunction = int (Base<T>::*)() noexcept;

template<class T>
using DerivedFunction = int (Derived<T>::*)() noexcept;

template<class T>
struct Source {
	operator BaseFunction<T>() const { return &Base<T>::get; }
};

struct DerivedSelection {
	char value;
};

struct EllipsisSelection {
	long long value;
};

DerivedSelection select(DerivedFunction<int>);
EllipsisSelection select(...);

static_assert(__is_same(decltype(select(Source<int>{})), DerivedSelection));

int main() {
	return sizeof(decltype(select(Source<int>{}))) == sizeof(DerivedSelection) ? 0 : 1;
}
